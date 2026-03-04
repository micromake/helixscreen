// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "memory_monitor.h"

#include <spdlog/spdlog.h>

#include <cstdio>
#include <cstring>

#ifdef __linux__
#include <fstream>
#include <sstream>
#include <string>
#endif

namespace helix {

static constexpr auto RATE_LIMIT_INTERVAL = std::chrono::minutes(5);

MemoryThresholds MemoryThresholds::for_device(const MemoryInfo& info) {
    MemoryThresholds t;

    if (info.is_constrained_device()) {
        // <256MB (AD5M ~110MB): very tight budgets
        t.warn_rss_kb = 15 * 1024;
        t.critical_rss_kb = 20 * 1024;
        t.warn_available_kb = 15 * 1024;
        t.critical_available_kb = 8 * 1024;
        t.growth_5min_kb = 1 * 1024;
    } else if (info.is_normal_device()) {
        // 256-512MB
        t.warn_rss_kb = 120 * 1024;
        t.critical_rss_kb = 180 * 1024;
        t.warn_available_kb = 32 * 1024;
        t.critical_available_kb = 16 * 1024;
        t.growth_5min_kb = 3 * 1024;
    } else {
        // >512MB (Pi 4/5 with 1-2GB+)
        t.warn_rss_kb = 180 * 1024;
        t.critical_rss_kb = 230 * 1024;
        t.warn_available_kb = 48 * 1024;
        t.critical_available_kb = 24 * 1024;
        t.growth_5min_kb = 5 * 1024;
    }

    return t;
}

const char* pressure_level_to_string(MemoryPressureLevel level) {
    switch (level) {
    case MemoryPressureLevel::none:
        return "none";
    case MemoryPressureLevel::elevated:
        return "elevated";
    case MemoryPressureLevel::warning:
        return "warning";
    case MemoryPressureLevel::critical:
        return "critical";
    }
    return "unknown";
}

MemoryMonitor& MemoryMonitor::instance() {
    static MemoryMonitor instance;
    return instance;
}

MemoryMonitor::~MemoryMonitor() {
    stop();
}

void MemoryMonitor::start(int interval_ms) {
    if (running_.load()) {
        return;
    }

    interval_ms_.store(interval_ms);
    running_.store(true);

    // Compute thresholds based on device tier
    auto sys_info = get_system_memory_info();
    thresholds_ = MemoryThresholds::for_device(sys_info);

    const char* tier = sys_info.is_constrained_device() ? "constrained"
                       : sys_info.is_normal_device()    ? "normal"
                                                        : "good";
    spdlog::info("[MemoryMonitor] Thresholds: warn_rss={}MB critical_rss={}MB "
                 "warn_avail={}MB critical_avail={}MB growth_5min={}MB (device tier: {}, "
                 "total={}MB)",
                 thresholds_.warn_rss_kb / 1024, thresholds_.critical_rss_kb / 1024,
                 thresholds_.warn_available_kb / 1024, thresholds_.critical_available_kb / 1024,
                 thresholds_.growth_5min_kb / 1024, tier, sys_info.total_mb());

    monitor_thread_ = std::thread([this]() { monitor_loop(); });

    spdlog::debug("[MemoryMonitor] Started (interval={}ms)", interval_ms);
}

void MemoryMonitor::stop() {
    if (!running_.load()) {
        return;
    }

    running_.store(false);

    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }

    spdlog::debug("[MemoryMonitor] Stopped");
}

void MemoryMonitor::set_warning_callback(WarningCallback cb) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    warning_callback_ = std::move(cb);
}

MemoryStats MemoryMonitor::get_current_stats() {
    MemoryStats stats;

#ifdef __linux__
    std::ifstream status("/proc/self/status");
    if (!status.is_open()) {
        return stats;
    }

    std::string line;
    while (std::getline(status, line)) {
        if (line.compare(0, 7, "VmSize:") == 0) {
            sscanf(line.c_str(), "VmSize: %zu", &stats.vm_size_kb);
        } else if (line.compare(0, 6, "VmRSS:") == 0) {
            sscanf(line.c_str(), "VmRSS: %zu", &stats.vm_rss_kb);
        } else if (line.compare(0, 7, "VmData:") == 0) {
            sscanf(line.c_str(), "VmData: %zu", &stats.vm_data_kb);
        } else if (line.compare(0, 7, "VmSwap:") == 0) {
            sscanf(line.c_str(), "VmSwap: %zu", &stats.vm_swap_kb);
        } else if (line.compare(0, 7, "VmPeak:") == 0) {
            sscanf(line.c_str(), "VmPeak: %zu", &stats.vm_peak_kb);
        } else if (line.compare(0, 6, "VmHWM:") == 0) {
            sscanf(line.c_str(), "VmHWM: %zu", &stats.vm_hwm_kb);
        }
    }
#endif

    return stats;
}

void MemoryMonitor::log_now(const char* context) {
    MemoryStats stats = get_current_stats();

    if (context) {
        spdlog::trace(
            "[MemoryMonitor] [{}] RSS={}kB VmSize={}kB VmData={}kB Swap={}kB (Peak: RSS={}kB "
            "Vm={}kB)",
            context, stats.vm_rss_kb, stats.vm_size_kb, stats.vm_data_kb, stats.vm_swap_kb,
            stats.vm_hwm_kb, stats.vm_peak_kb);
    } else {
        spdlog::trace("[MemoryMonitor] RSS={}kB VmSize={}kB VmData={}kB Swap={}kB (Peak: RSS={}kB "
                      "Vm={}kB)",
                      stats.vm_rss_kb, stats.vm_size_kb, stats.vm_data_kb, stats.vm_swap_kb,
                      stats.vm_hwm_kb, stats.vm_peak_kb);
    }
}

void MemoryMonitor::evaluate_thresholds(const MemoryStats& stats) {
    auto sys_info = get_system_memory_info();
    MemoryPressureLevel level = MemoryPressureLevel::none;
    std::string reason;

    // Check RSS against thresholds (highest severity first)
    if (stats.vm_rss_kb >= thresholds_.critical_rss_kb) {
        level = MemoryPressureLevel::critical;
        reason = fmt::format("RSS {}MB exceeds critical threshold {}MB", stats.vm_rss_kb / 1024,
                             thresholds_.critical_rss_kb / 1024);
    } else if (stats.vm_rss_kb >= thresholds_.warn_rss_kb) {
        level = MemoryPressureLevel::warning;
        reason = fmt::format("RSS {}MB exceeds warning threshold {}MB", stats.vm_rss_kb / 1024,
                             thresholds_.warn_rss_kb / 1024);
    }

    // Check available system memory (may escalate level)
    if (sys_info.available_kb > 0) {
        if (sys_info.available_kb <= thresholds_.critical_available_kb &&
            level < MemoryPressureLevel::critical) {
            level = MemoryPressureLevel::critical;
            reason = fmt::format("System available {}MB below critical threshold {}MB",
                                 sys_info.available_mb(), thresholds_.critical_available_kb / 1024);
        } else if (sys_info.available_kb <= thresholds_.warn_available_kb &&
                   level < MemoryPressureLevel::warning) {
            level = MemoryPressureLevel::warning;
            reason = fmt::format("System available {}MB below warning threshold {}MB",
                                 sys_info.available_mb(), thresholds_.warn_available_kb / 1024);
        }
    }

    // Check growth rate (may trigger elevated)
    int64_t growth_kb = 0;
    if (rss_history_count_ >= RSS_HISTORY_SIZE) {
        size_t oldest_index = rss_history_index_; // Next slot is the oldest when full
        growth_kb = static_cast<int64_t>(stats.vm_rss_kb) -
                    static_cast<int64_t>(rss_history_[oldest_index]);

        if (growth_kb > static_cast<int64_t>(thresholds_.growth_5min_kb)) {
            if (level < MemoryPressureLevel::elevated) {
                level = MemoryPressureLevel::elevated;
                reason = fmt::format("RSS growth {:+}KB over 5 minutes exceeds threshold {}KB",
                                     growth_kb, thresholds_.growth_5min_kb);
            }
        }
    }

    // Update atomic level
    pressure_level_.store(level);

    // Log and fire callback if we have a non-none level
    if (level != MemoryPressureLevel::none) {
        // Rate limit: max 1 callback per level per 5 minutes
        auto now = std::chrono::steady_clock::now();
        int level_idx = static_cast<int>(level);
        if (now - last_warning_time_[level_idx] >= RATE_LIMIT_INTERVAL) {
            last_warning_time_[level_idx] = now;

            // Log at appropriate severity
            switch (level) {
            case MemoryPressureLevel::elevated:
                spdlog::info("[MemoryMonitor] ELEVATED: {}", reason);
                break;
            case MemoryPressureLevel::warning:
                spdlog::warn("[MemoryMonitor] WARNING: {}", reason);
                break;
            case MemoryPressureLevel::critical:
                spdlog::error("[MemoryMonitor] CRITICAL: {}", reason);
                break;
            default:
                break;
            }

            fire_warning(level, reason, stats, growth_kb);
        }
    }
}

void MemoryMonitor::fire_warning(MemoryPressureLevel level, const std::string& reason,
                                 const MemoryStats& stats, int64_t growth_kb) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (!warning_callback_) {
        return;
    }

    MemoryWarningEvent event;
    event.level = level;
    event.reason = reason;
    event.stats = stats;
    event.system_info = get_system_memory_info();
    event.growth_5min_kb = growth_kb;
    read_smaps_rollup(event.smaps);

    warning_callback_(event);
}

void MemoryMonitor::monitor_loop() {
    log_now("start");

    MemoryStats prev_stats = get_current_stats();

    while (running_.load()) {
        // Sleep in small chunks so we can respond to stop() quickly
        int remaining_ms = interval_ms_.load();
        while (remaining_ms > 0 && running_.load()) {
            int sleep_ms = std::min(remaining_ms, 100);
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
            remaining_ms -= sleep_ms;
        }

        if (!running_.load()) {
            break;
        }

        MemoryStats stats = get_current_stats();

        // Calculate deltas
        int64_t rss_delta =
            static_cast<int64_t>(stats.vm_rss_kb) - static_cast<int64_t>(prev_stats.vm_rss_kb);
        int64_t vm_delta =
            static_cast<int64_t>(stats.vm_size_kb) - static_cast<int64_t>(prev_stats.vm_size_kb);

        // Log with delta if significant change (>100kB)
        if (std::abs(rss_delta) > 100 || std::abs(vm_delta) > 100) {
            spdlog::trace(
                "[MemoryMonitor] RSS={}kB ({:+}kB) VmSize={}kB ({:+}kB) VmData={}kB Swap={}kB",
                stats.vm_rss_kb, rss_delta, stats.vm_size_kb, vm_delta, stats.vm_data_kb,
                stats.vm_swap_kb);
        } else {
            spdlog::trace("[MemoryMonitor] RSS={}kB VmSize={}kB VmData={}kB Swap={}kB",
                          stats.vm_rss_kb, stats.vm_size_kb, stats.vm_data_kb, stats.vm_swap_kb);
        }

        // Evaluate pressure thresholds
        evaluate_thresholds(stats);

        // Deep sample every 6th tick (30s at 5s interval): record RSS for growth tracking
        ++deep_sample_counter_;
        if (deep_sample_counter_ >= 6) {
            deep_sample_counter_ = 0;

            rss_history_[rss_history_index_] = stats.vm_rss_kb;
            rss_history_index_ = (rss_history_index_ + 1) % RSS_HISTORY_SIZE;
            if (rss_history_count_ < RSS_HISTORY_SIZE) {
                ++rss_history_count_;
            }
        }

        prev_stats = stats;
    }

    log_now("stop");
}

} // namespace helix
