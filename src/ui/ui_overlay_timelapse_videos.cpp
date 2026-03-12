// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_overlay_timelapse_videos.h"

#include "helix-xml/src/xml/lv_xml.h"
#include "static_panel_registry.h"
#include "timelapse_thumbnailer.h"
#include "ui_callback_helpers.h"
#include "ui_modal.h"
#include "ui_nav_manager.h"
#include "ui_update_queue.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// ============================================================================
// GLOBAL INSTANCE
// ============================================================================

static std::unique_ptr<TimelapseVideosOverlay> g_timelapse_videos;
static lv_obj_t* g_timelapse_videos_panel = nullptr;

TimelapseVideosOverlay& get_global_timelapse_videos() {
    if (!g_timelapse_videos) {
        spdlog::error("[Timelapse Videos] get_global_timelapse_videos() called before initialization!");
        throw std::runtime_error("TimelapseVideosOverlay not initialized");
    }
    return *g_timelapse_videos;
}

void init_global_timelapse_videos(MoonrakerAPI* api) {
    if (g_timelapse_videos) {
        spdlog::warn("[Timelapse Videos] TimelapseVideosOverlay already initialized, skipping");
        return;
    }
    g_timelapse_videos = std::make_unique<TimelapseVideosOverlay>(api);
    StaticPanelRegistry::instance().register_destroy("TimelapseVideosOverlay", []() {
        if (g_timelapse_videos_panel) {
            NavigationManager::instance().unregister_overlay_instance(g_timelapse_videos_panel);
        }
        g_timelapse_videos_panel = nullptr;
        g_timelapse_videos.reset();
    });
    spdlog::trace("[Timelapse Videos] TimelapseVideosOverlay initialized");
}

void open_timelapse_videos() {
    spdlog::debug("[Timelapse Videos] Opening timelapse videos overlay");

    if (!g_timelapse_videos) {
        spdlog::error("[Timelapse Videos] Global instance not initialized!");
        return;
    }

    // Lazy-create the panel
    if (!g_timelapse_videos_panel) {
        spdlog::debug("[Timelapse Videos] Creating timelapse videos panel...");
        g_timelapse_videos_panel =
            g_timelapse_videos->create(lv_display_get_screen_active(nullptr));

        if (g_timelapse_videos_panel) {
            NavigationManager::instance().register_overlay_instance(g_timelapse_videos_panel,
                                                                    g_timelapse_videos.get());
            spdlog::debug("[Timelapse Videos] Panel created and registered");
        } else {
            spdlog::error("[Timelapse Videos] Failed to create timelapse_videos_overlay");
            return;
        }
    }

    // Show the overlay - NavigationManager will call on_activate()
    NavigationManager::instance().push_overlay(g_timelapse_videos_panel);
}

// ============================================================================
// CONSTRUCTOR
// ============================================================================

TimelapseVideosOverlay::TimelapseVideosOverlay(MoonrakerAPI* api)
    : api_(api), alive_(std::make_shared<std::atomic<bool>>(true)) {
    spdlog::debug("[{}] Constructor", get_name());
}

// ============================================================================
// LIFECYCLE
// ============================================================================

void TimelapseVideosOverlay::init_subjects() {
    spdlog::debug("[{}] init_subjects()", get_name());

    // Register XML callbacks for the render button
    register_xml_callbacks({
        {"on_timelapse_render_now", on_render_now},
    });
}

lv_obj_t* TimelapseVideosOverlay::create(lv_obj_t* parent) {
    overlay_root_ =
        static_cast<lv_obj_t*>(lv_xml_create(parent, "timelapse_videos_overlay", nullptr));
    if (!overlay_root_) {
        spdlog::error("[{}] Failed to create overlay from XML", get_name());
        return nullptr;
    }

    spdlog::debug("[{}] create() - finding widgets", get_name());

    video_grid_container_ = lv_obj_find_by_name(overlay_root_, "video_grid_container");
    video_grid_empty_ = lv_obj_find_by_name(overlay_root_, "video_grid_empty");

    spdlog::debug("[{}] Widgets found: grid_container={} grid_empty={}", get_name(),
                  video_grid_container_ != nullptr, video_grid_empty_ != nullptr);

    return overlay_root_;
}

void TimelapseVideosOverlay::on_activate() {
    OverlayBase::on_activate();
    nav_generation_.fetch_add(1);
    spdlog::debug("[{}] on_activate() - fetching video list", get_name());
    detect_playback_capability();
    fetch_video_list();
}

void TimelapseVideosOverlay::on_deactivate() {
    OverlayBase::on_deactivate();
    nav_generation_.fetch_add(1);
    clear_video_grid();
    spdlog::debug("[{}] on_deactivate()", get_name());
}

void TimelapseVideosOverlay::cleanup() {
    spdlog::debug("[{}] cleanup()", get_name());
    if (alive_) {
        alive_->store(false);
    }
    OverlayBase::cleanup();
}

// ============================================================================
// VIDEO LIST FETCHING
// ============================================================================

void TimelapseVideosOverlay::fetch_video_list() {
    if (!api_) {
        spdlog::debug("[{}] No API available", get_name());
        return;
    }

    auto alive = alive_;
    uint32_t gen = nav_generation_.load();

    api_->files().list_files(
        "timelapse", "", false,
        [this, alive, gen](const std::vector<FileInfo>& files) {
            if (!alive || !alive->load()) return;
            helix::ui::queue_update([this, files, alive, gen]() {
                if (!alive->load() || gen != nav_generation_.load()) return;
                populate_video_grid(files);
            });
        },
        [this](const MoonrakerError& error) {
            spdlog::error("[{}] Failed to fetch video list: {}", get_name(), error.message);
        });
}

// ============================================================================
// VIDEO GRID MANAGEMENT
// ============================================================================

/// Format byte size to human-readable string
static std::string format_file_size(uint64_t bytes) {
    if (bytes < 1024) {
        return std::to_string(bytes) + " B";
    }
    if (bytes < 1024 * 1024) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1f KB", static_cast<double>(bytes) / 1024.0);
        return buf;
    }
    if (bytes < 1024ULL * 1024 * 1024) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1f MB",
                 static_cast<double>(bytes) / (1024.0 * 1024.0));
        return buf;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f GB",
             static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
    return buf;
}

/// Format unix timestamp to "Mon DD" string
static std::string format_date_short(double modified) {
    auto t = static_cast<time_t>(modified);
    struct tm tm_buf {};
    localtime_r(&t, &tm_buf);
    char buf[32];
    strftime(buf, sizeof(buf), "%b %d", &tm_buf);
    return buf;
}

void TimelapseVideosOverlay::populate_video_grid(const std::vector<FileInfo>& files) {
    clear_video_grid();
    videos_.clear();

    // Filter to video files only and build entries
    for (const auto& file : files) {
        if (!helix::timelapse::TimelapseThumbnailer::is_video_file(file.filename)) {
            continue;
        }
        VideoEntry entry;
        entry.filename = file.filename;
        entry.size = file.size;
        entry.modified = file.modified;
        entry.file_info = format_file_size(file.size) + " \xC2\xB7 " + format_date_short(file.modified);
        videos_.push_back(std::move(entry));
    }

    // Sort newest first
    std::sort(videos_.begin(), videos_.end(),
              [](const VideoEntry& a, const VideoEntry& b) { return a.modified > b.modified; });

    spdlog::info("[{}] Found {} timelapse videos", get_name(), videos_.size());

    if (videos_.empty()) {
        // Show empty state, hide grid
        if (video_grid_container_) {
            lv_obj_add_flag(video_grid_container_, LV_OBJ_FLAG_HIDDEN);
        }
        if (video_grid_empty_) {
            lv_obj_remove_flag(video_grid_empty_, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    // Show grid, hide empty state
    if (video_grid_container_) {
        lv_obj_remove_flag(video_grid_container_, LV_OBJ_FLAG_HIDDEN);
    }
    if (video_grid_empty_) {
        lv_obj_add_flag(video_grid_empty_, LV_OBJ_FLAG_HIDDEN);
    }

    if (!video_grid_container_) return;

    for (const auto& video : videos_) {
        const char* attrs[] = {"filename",  video.filename.c_str(),
                               "file_info", video.file_info.c_str(),
                               nullptr};

        lv_obj_t* card = static_cast<lv_obj_t*>(
            lv_xml_create(video_grid_container_, "timelapse_video_card", attrs));

        if (!card) {
            spdlog::warn("[{}] Failed to create card for '{}'", get_name(), video.filename);
            continue;
        }

        // Store filename in user_data for click handler (heap-allocated copy)
        char* filename_copy =
            static_cast<char*>(lv_malloc(video.filename.size() + 1));
        std::memcpy(filename_copy, video.filename.c_str(), video.filename.size() + 1);
        lv_obj_set_user_data(card, filename_copy);

        // NOTE: lv_obj_add_event_cb used here (not XML event_cb) because each dynamically
        // created card needs per-instance user_data (filename) that XML bindings can't provide.
        lv_obj_add_event_cb(card, on_card_clicked, LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(card, on_card_long_pressed, LV_EVENT_LONG_PRESSED, this);
        lv_obj_add_event_cb(card, on_card_delete, LV_EVENT_DELETE, nullptr);

        // Show/hide play overlay based on playback capability
        lv_obj_t* play_overlay = lv_obj_find_by_name(card, "play_overlay");
        if (play_overlay) {
            if (can_play_) {
                lv_obj_remove_flag(play_overlay, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(play_overlay, LV_OBJ_FLAG_HIDDEN);
            }
        }

        // Show no-thumbnail placeholder by default (thumbnail loading is Task 12)
        lv_obj_t* no_thumb_icon = lv_obj_find_by_name(card, "no_thumbnail_icon");
        if (no_thumb_icon) {
            lv_obj_remove_flag(no_thumb_icon, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void TimelapseVideosOverlay::clear_video_grid() {
    if (video_grid_container_) {
        auto freeze = helix::ui::UpdateQueue::instance().scoped_freeze();
        helix::ui::UpdateQueue::instance().drain();
        lv_obj_clean(video_grid_container_);
    }
    videos_.clear();
}

// ============================================================================
// PLAYBACK CAPABILITY
// ============================================================================

void TimelapseVideosOverlay::detect_playback_capability() {
    can_play_ = false;
    player_command_.clear();

    // Check if we're running on the same host as Moonraker
    if (api_) {
        std::string ws_url = api_->get_websocket_url();
        // Extract host from ws://host:port/...
        std::string host;
        auto scheme_end = ws_url.find("://");
        if (scheme_end != std::string::npos) {
            auto host_start = scheme_end + 3;
            auto host_end = ws_url.find(':', host_start);
            if (host_end == std::string::npos) {
                host_end = ws_url.find('/', host_start);
            }
            if (host_end != std::string::npos) {
                host = ws_url.substr(host_start, host_end - host_start);
            } else {
                host = ws_url.substr(host_start);
            }
        }
        is_local_moonraker_ = helix::timelapse::is_local_host(host);
    }

    // Check for available players
    FILE* pipe = popen("which mpv 2>/dev/null", "r");
    if (pipe) {
        char buf[256];
        if (fgets(buf, sizeof(buf), pipe) != nullptr) {
            // mpv found
            player_command_ = "mpv";
            can_play_ = true;
        }
        pclose(pipe);
    }

    if (!can_play_) {
        pipe = popen("which ffplay 2>/dev/null", "r");
        if (pipe) {
            char buf[256];
            if (fgets(buf, sizeof(buf), pipe) != nullptr) {
                player_command_ = "ffplay";
                can_play_ = true;
            }
            pclose(pipe);
        }
    }

    spdlog::debug("[{}] Playback capability: can_play={} player='{}' local={}", get_name(),
                  can_play_, player_command_, is_local_moonraker_);
}

// ============================================================================
// VIDEO PLAYBACK
// ============================================================================

void TimelapseVideosOverlay::play_video(const std::string& filename) {
    if (!can_play_ || player_command_.empty()) {
        spdlog::warn("[{}] No video player available", get_name());
        return;
    }

    if (is_local_moonraker_) {
        // Local: construct path directly
        std::string path = std::string(getenv("HOME") ? getenv("HOME") : "/root") +
                           "/printer_data/timelapse/" + filename;
        std::string cmd = helix::timelapse::build_player_command(player_command_, path);
        spdlog::info("[{}] Playing local video: {}", get_name(), cmd);

        pid_t pid = fork();
        if (pid == 0) {
            // Child process
            execlp("/bin/sh", "sh", "-c", cmd.c_str(), nullptr);
            _exit(127);
        } else if (pid < 0) {
            spdlog::error("[{}] fork() failed for video playback", get_name());
        }
    } else {
        // Remote: download to /tmp then play
        if (!api_) return;

        // Ensure temp directory exists
        mkdir("/tmp/helix_timelapse", 0755);

        auto alive = alive_;
        uint32_t gen = nav_generation_.load();
        std::string player = player_command_;

        spdlog::info("[{}] Downloading remote video '{}' for playback", get_name(), filename);

        std::string dest_path = "/tmp/helix_timelapse/" + filename;

        api_->transfers().download_file_to_path(
            "timelapse", filename, dest_path,
            [this, alive, gen, dest_path, player](const std::string& /*path*/) {
                if (!alive || !alive->load()) return;
                helix::ui::queue_update([this, alive, gen, dest_path, player]() {
                    if (!alive->load() || gen != nav_generation_.load()) return;

                    std::string cmd = helix::timelapse::build_player_command(player, dest_path);
                    spdlog::info("[{}] Playing downloaded video: {}", get_name(), cmd);

                    pid_t pid = fork();
                    if (pid == 0) {
                        execlp("/bin/sh", "sh", "-c", cmd.c_str(), nullptr);
                        _exit(127);
                    } else if (pid < 0) {
                        spdlog::error("[{}] fork() failed for video playback", get_name());
                    }
                });
            },
            [this](const MoonrakerError& error) {
                spdlog::error("[{}] Failed to download video: {}", get_name(), error.message);
            });
    }
}

// ============================================================================
// DELETE CONFIRMATION
// ============================================================================

void TimelapseVideosOverlay::confirm_delete(const std::string& filename) {
    pending_delete_filename_ = filename;

    std::string message = "Delete " + filename + "?";

    delete_confirmation_dialog_ = helix::ui::modal_show_confirmation(
        "Delete Video", message.c_str(), ModalSeverity::Warning, "Delete", on_delete_confirmed,
        on_delete_cancelled, this);
}

void TimelapseVideosOverlay::on_delete_confirmed(lv_event_t* e) {
    auto* self = static_cast<TimelapseVideosOverlay*>(lv_event_get_user_data(e));
    if (!self || !g_timelapse_videos) return;

    // Hide the dialog
    if (self->delete_confirmation_dialog_) {
        helix::ui::modal_hide(self->delete_confirmation_dialog_);
        self->delete_confirmation_dialog_ = nullptr;
    }

    if (!self->api_ || self->pending_delete_filename_.empty()) return;

    std::string full_path = "timelapse/" + self->pending_delete_filename_;
    auto alive = self->alive_;

    spdlog::info("[Timelapse Videos] Deleting video: {}", self->pending_delete_filename_);

    self->api_->files().delete_file(
        full_path,
        [self, alive]() {
            if (!alive || !alive->load()) return;
            helix::ui::queue_update([self, alive]() {
                if (!alive->load()) return;
                spdlog::info("[Timelapse Videos] Video deleted, refreshing list");
                self->fetch_video_list();
            });
        },
        [](const MoonrakerError& error) {
            spdlog::error("[Timelapse Videos] Failed to delete video: {}", error.message);
        });

    self->pending_delete_filename_.clear();
}

void TimelapseVideosOverlay::on_delete_cancelled(lv_event_t* e) {
    auto* self = static_cast<TimelapseVideosOverlay*>(lv_event_get_user_data(e));
    if (!self) return;

    if (self->delete_confirmation_dialog_) {
        helix::ui::modal_hide(self->delete_confirmation_dialog_);
        self->delete_confirmation_dialog_ = nullptr;
    }
    self->pending_delete_filename_.clear();
}

// ============================================================================
// STATIC EVENT HANDLERS
// ============================================================================

void TimelapseVideosOverlay::on_render_now(lv_event_t* /*e*/) {
    if (!g_timelapse_videos || !g_timelapse_videos->api_) {
        spdlog::warn("[Timelapse Videos] Render requested but no API available");
        return;
    }

    spdlog::info("[Timelapse Videos] Rendering timelapse...");
    g_timelapse_videos->api_->timelapse().render_timelapse(
        []() { spdlog::info("[Timelapse Videos] Render started successfully"); },
        [](const MoonrakerError& error) {
            spdlog::error("[Timelapse Videos] Render failed: {}", error.message);
        });
}

void TimelapseVideosOverlay::on_card_clicked(lv_event_t* e) {
    auto* self = static_cast<TimelapseVideosOverlay*>(lv_event_get_user_data(e));
    if (!self) return;

    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    auto* filename = static_cast<const char*>(lv_obj_get_user_data(target));
    if (!filename) return;

    spdlog::debug("[Timelapse Videos] Card clicked: {}", filename);

    if (self->can_play_) {
        self->play_video(filename);
    }
}

void TimelapseVideosOverlay::on_card_long_pressed(lv_event_t* e) {
    auto* self = static_cast<TimelapseVideosOverlay*>(lv_event_get_user_data(e));
    if (!self) return;

    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    auto* filename = static_cast<const char*>(lv_obj_get_user_data(target));
    if (!filename) return;

    spdlog::debug("[Timelapse Videos] Card long-pressed: {}", filename);
    self->confirm_delete(filename);
}

void TimelapseVideosOverlay::on_card_delete(lv_event_t* e) {
    // Free the heap-allocated filename when the card is destroyed
    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    auto* filename = static_cast<char*>(lv_obj_get_user_data(target));
    if (filename) {
        lv_free(filename);
        lv_obj_set_user_data(target, nullptr);
    }
}
