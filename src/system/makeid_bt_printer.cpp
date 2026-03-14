// SPDX-License-Identifier: GPL-3.0-or-later

#include "makeid_bt_printer.h"
#include "bluetooth_loader.h"
#include "makeid_protocol.h"
#include "ui_update_queue.h"

#include <spdlog/spdlog.h>

#include <mutex>
#include <thread>

namespace helix::label {

// Serialize BLE print jobs and protect persistent connection state
static std::mutex s_print_mutex;

// Persistent BLE connection — survives across prints
static helix_bt_context* s_ctx = nullptr;
static int s_handle = -1;
static std::string s_connected_mac;

/// Helper: format bytes as hex string for logging
static std::string hex_dump(const uint8_t* data, int len) {
    std::string hex;
    for (int i = 0; i < len; i++) {
        if (!hex.empty()) hex += ' ';
        hex += fmt::format("{:02X}", data[i]);
    }
    return hex;
}

/// Read a response and parse it. Returns the parsed response.
/// If no read capability, returns Success (optimistic fire-and-forget).
static MakeIdResponse read_and_parse(helix::bluetooth::BluetoothLoader& loader,
                                      helix_bt_context* ctx, int handle,
                                      const char* label, int timeout_ms = 3000) {
    if (!loader.ble_read) {
        return {MakeIdResponseStatus::Success};
    }

    uint8_t resp[64];
    int n = loader.ble_read(ctx, handle, resp, sizeof(resp), timeout_ms);
    if (n > 0) {
        spdlog::debug("MakeID BT: {} response ({} bytes): {}", label, n, hex_dump(resp, n));
        return makeid_parse_response(resp, static_cast<size_t>(n));
    } else if (n == 0) {
        spdlog::warn("MakeID BT: {} response timeout", label);
        return {MakeIdResponseStatus::Resend};
    } else {
        spdlog::warn("MakeID BT: {} response read error: {}", label, n);
        return {MakeIdResponseStatus::Error};
    }
}

/// Try a handshake to check if the connection is still alive
static bool connection_alive(helix::bluetooth::BluetoothLoader& loader) {
    if (!s_ctx || s_handle < 0) return false;

    auto pkt = makeid_build_handshake(MakeIdHandshakeState::Search);
    int ret = loader.ble_write(s_ctx, s_handle, pkt.data(), static_cast<int>(pkt.size()));
    if (ret < 0) {
        spdlog::debug("MakeID BT: persistent connection dead (write failed), will reconnect");
        return false;
    }

    if (loader.ble_read) {
        uint8_t resp[64];
        int n = loader.ble_read(s_ctx, s_handle, resp, sizeof(resp), 500);
        if (n <= 0) {
            spdlog::debug("MakeID BT: persistent connection dead (no handshake response), will reconnect");
            return false;
        }
    }
    return true;
}

/// Ensure we have a live BLE connection to the given MAC, reusing existing if possible
static int ensure_connected(helix::bluetooth::BluetoothLoader& loader, const std::string& mac) {
    if (s_connected_mac == mac && connection_alive(loader)) {
        spdlog::debug("MakeID BT: reusing persistent connection (handle={})", s_handle);
        return s_handle;
    }

    // Clean up stale connection
    if (s_ctx) {
        if (s_handle >= 0) {
            loader.disconnect(s_ctx, s_handle);
            s_handle = -1;
        }
        loader.deinit(s_ctx);
        s_ctx = nullptr;
    }
    s_connected_mac.clear();

    // Create fresh connection
    s_ctx = loader.init();
    if (!s_ctx) return -1;

    s_handle = loader.connect_ble(s_ctx, mac.c_str(), MAKEID_WRITE_CHAR_UUID);
    if (s_handle < 0) {
        loader.deinit(s_ctx);
        s_ctx = nullptr;
        return -1;
    }

    s_connected_mac = mac;
    spdlog::info("MakeID BT: new persistent connection (handle={})", s_handle);

    // BLE connection settle time — printer firmware may silently discard
    // data received before its BLE stack is fully initialized
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // Send handshake (0x10) as session initialization
    auto handshake = makeid_build_handshake(MakeIdHandshakeState::Search);
    loader.ble_write(s_ctx, s_handle, handshake.data(), static_cast<int>(handshake.size()));
    if (loader.ble_read) {
        uint8_t resp[64];
        int n = loader.ble_read(s_ctx, s_handle, resp, sizeof(resp), 2000);
        if (n > 0) {
            spdlog::debug("MakeID BT: initial handshake response: {}", hex_dump(resp, n));
        }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    spdlog::info("MakeID BT: connection initialized");

    return s_handle;
}

/// Poll handshake until printer reports ready (success state)
static bool wait_for_ready(helix::bluetooth::BluetoothLoader& loader, int handle,
                            int max_polls = 20) {
    for (int poll = 0; poll < max_polls; poll++) {
        auto pkt = makeid_build_handshake(MakeIdHandshakeState::Search);
        int ret = loader.ble_write(s_ctx, handle, pkt.data(), static_cast<int>(pkt.size()));
        if (ret < 0) return false;

        auto resp = read_and_parse(loader, s_ctx, handle, "handshake-poll", 2000);
        if (resp.status == MakeIdResponseStatus::Success) {
            return true;
        }
        if (resp.status == MakeIdResponseStatus::Error ||
            resp.status == MakeIdResponseStatus::Exit) {
            spdlog::warn("MakeID BT: printer error/exit during handshake poll (code={})",
                          resp.error_code);
            return false;
        }
        // Wait or other transient state — keep polling
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    spdlog::warn("MakeID BT: handshake poll timed out after {} attempts", max_polls);
    return false;
}

void MakeIdBluetoothPrinter::set_device(const std::string& mac, const std::string& device_name) {
    mac_ = mac;
    name_ = device_name;
}

std::string MakeIdBluetoothPrinter::name() const {
    return "MakeID (Bluetooth)";
}

std::vector<LabelSize> MakeIdBluetoothPrinter::supported_sizes() const {
    return makeid_default_sizes();
}

void MakeIdBluetoothPrinter::print(const LabelBitmap& bitmap, const LabelSize& size,
                                     PrintCallback callback) {
    auto& loader = helix::bluetooth::BluetoothLoader::instance();
    if (!loader.is_available()) {
        spdlog::error("MakeID BT: Bluetooth not available");
        helix::ui::queue_update([callback]() {
            if (callback) callback(false, "Bluetooth not available");
        });
        return;
    }

    if (mac_.empty()) {
        spdlog::error("MakeID BT: No device configured");
        helix::ui::queue_update([callback]() {
            if (callback) callback(false, "Bluetooth device not configured");
        });
        return;
    }

    // Log bitmap stats for debugging
    {
        int black_pixels = 0;
        for (int y = 0; y < bitmap.height(); y++) {
            const uint8_t* row = bitmap.row_data(y);
            for (int b = 0; b < bitmap.row_byte_width(); b++) {
                uint8_t v = row[b];
                while (v) { black_pixels++; v &= (v - 1); }
            }
        }
        spdlog::debug("MakeID BT: bitmap {}x{}, {} black pixels",
                      bitmap.width(), bitmap.height(), black_pixels);
    }

    // Determine printer width from label size
    int printer_width_bytes = (size.width_px + 7) / 8;

    MakeIdPrintJobConfig config;
    config.printer_width_bytes = printer_width_bytes;
    config.max_rows_per_chunk = 56;

    auto job = makeid_build_print_job(bitmap, size, config);
    spdlog::info("MakeID BT: {} chunks, {} rows to {} via BLE",
                 job.chunks.size(), job.total_rows, mac_);

    std::string mac = mac_;

    std::thread([mac, job = std::move(job), callback]() {
        std::lock_guard<std::mutex> lock(s_print_mutex);

        auto& loader = helix::bluetooth::BluetoothLoader::instance();
        bool success = false;
        std::string error;

        int handle = ensure_connected(loader, mac);
        if (handle < 0) {
            const char* err = (s_ctx && loader.last_error) ? loader.last_error(s_ctx) : "unknown error";
            error = fmt::format("BLE connect failed: {}", err);
            spdlog::error("MakeID BT: {}", error);
        } else {
            // Poll handshake until printer is ready
            if (!wait_for_ready(loader, handle)) {
                error = "Printer not ready (handshake failed)";
                spdlog::error("MakeID BT: {}", error);
            } else {
                // Send all print data chunks sequentially
                // MakeID protocol requires ACK on ABF2 between each frame
                bool write_ok = true;
                for (size_t i = 0; i < job.chunks.size(); i++) {
                    const auto& frame = job.chunks[i];
                    int ret = loader.ble_write(s_ctx, handle, frame.data(),
                                                static_cast<int>(frame.size()));
                    if (ret < 0) {
                        const char* err = loader.last_error ? loader.last_error(s_ctx) : "unknown error";
                        error = fmt::format("BLE write failed at chunk {}: {}", i, err);
                        spdlog::error("MakeID BT: {}", error);
                        write_ok = false;
                        s_handle = -1;
                        s_connected_mac.clear();
                        break;
                    }

                    // Wait for ACK before sending next chunk
                    auto label = fmt::format("chunk[{}]", i);
                    int retries = 0;
                    constexpr int max_retries = 10;

                    while (retries < max_retries) {
                        auto resp = read_and_parse(loader, s_ctx, handle, label.c_str(), 5000);

                        if (resp.status == MakeIdResponseStatus::Success) {
                            break;
                        } else if (resp.status == MakeIdResponseStatus::Wait) {
                            spdlog::debug("MakeID BT: chunk[{}] wait, polling...", i);
                            // Printer busy — poll handshake until ready, then the ACK
                            // should come through
                            std::this_thread::sleep_for(std::chrono::milliseconds(500));
                            retries++;
                        } else if (resp.status == MakeIdResponseStatus::Resend) {
                            spdlog::debug("MakeID BT: chunk[{}] resend requested, retrying", i);
                            ret = loader.ble_write(s_ctx, handle, frame.data(),
                                                    static_cast<int>(frame.size()));
                            if (ret < 0) {
                                error = fmt::format("BLE resend failed at chunk {}", i);
                                spdlog::error("MakeID BT: {}", error);
                                write_ok = false;
                                s_handle = -1;
                                s_connected_mac.clear();
                                break;
                            }
                            retries++;
                        } else if (resp.status == MakeIdResponseStatus::Error) {
                            error = fmt::format("Printer error at chunk {} (code={})",
                                               i, resp.error_code);
                            spdlog::error("MakeID BT: {}", error);
                            write_ok = false;
                            break;
                        } else {
                            // Null or unexpected — treat as timeout, continue
                            spdlog::debug("MakeID BT: chunk[{}] unexpected response status, continuing", i);
                            break;
                        }
                    }

                    if (!write_ok) break;
                }

                if (write_ok) {
                    spdlog::info("MakeID BT: all {} chunks sent, sending cancel to finalize",
                                 job.chunks.size());

                    // Send cancel handshake to signal end of print job
                    auto cancel = makeid_build_handshake(MakeIdHandshakeState::Cancel);
                    loader.ble_write(s_ctx, handle, cancel.data(),
                                      static_cast<int>(cancel.size()));

                    // Read final response
                    read_and_parse(loader, s_ctx, handle, "cancel", 2000);

                    success = true;
                    spdlog::info("MakeID BT: print job complete");
                }
            }

            // Keep connection alive for subsequent prints
        }

        helix::ui::queue_update([callback, success, error]() {
            if (callback) callback(success, error);
        });
    }).detach();
}

}  // namespace helix::label
