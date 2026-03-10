// SPDX-License-Identifier: GPL-3.0-or-later

#include "niimbot_bt_printer.h"
#include "bluetooth_loader.h"
#include "niimbot_protocol.h"
#include "ui_update_queue.h"

#include <spdlog/spdlog.h>

#include <mutex>
#include <thread>

namespace helix::label {

// Serialize BLE print jobs
static std::mutex s_print_mutex;

void NiimbotBluetoothPrinter::set_device(const std::string& mac, const std::string& device_name) {
    mac_ = mac;
    name_ = device_name;
}

std::string NiimbotBluetoothPrinter::name() const {
    return "Niimbot (Bluetooth)";
}

std::vector<LabelSize> NiimbotBluetoothPrinter::supported_sizes() const {
    return niimbot_sizes_for_model(name_);
}

void NiimbotBluetoothPrinter::print(const LabelBitmap& bitmap, const LabelSize& size,
                                     PrintCallback callback) {
    auto& loader = helix::bluetooth::BluetoothLoader::instance();
    if (!loader.is_available()) {
        spdlog::error("Niimbot BT: Bluetooth not available");
        helix::ui::queue_update([callback]() {
            if (callback) callback(false, "Bluetooth not available");
        });
        return;
    }

    if (mac_.empty()) {
        spdlog::error("Niimbot BT: No device configured");
        helix::ui::queue_update([callback]() {
            if (callback) callback(false, "Bluetooth device not configured");
        });
        return;
    }

    auto job = niimbot_build_print_job(bitmap, size);
    spdlog::info("Niimbot BT: {} packets, {} rows to {} via BLE",
                 job.packets.size(), job.total_rows, mac_);

    std::string mac = mac_;

    // All state needed by the detached thread is captured by value/move.
    // The NiimbotBluetoothPrinter object may be destroyed before thread completes.
    std::thread([mac, job = std::move(job), callback]() {
        std::lock_guard<std::mutex> lock(s_print_mutex);

        auto& loader = helix::bluetooth::BluetoothLoader::instance();
        bool success = false;
        std::string error;

        auto* ctx = loader.init();
        if (!ctx) {
            error = "Failed to initialize Bluetooth context";
            spdlog::error("Niimbot BT: {}", error);
        } else {
            int handle = loader.connect_ble(ctx, mac.c_str(), NIIMBOT_SERVICE_UUID);
            if (handle < 0) {
                const char* err = loader.last_error ? loader.last_error(ctx) : "unknown error";
                error = fmt::format("BLE connect failed: {}", err);
                spdlog::error("Niimbot BT: {}", error);
            } else {
                // Send all packets sequentially with inter-packet delay
                bool write_ok = true;
                for (size_t i = 0; i < job.packets.size(); i++) {
                    const auto& pkt = job.packets[i];
                    int ret = loader.ble_write(ctx, handle, pkt.data(),
                                               static_cast<int>(pkt.size()));
                    if (ret < 0) {
                        const char* err = loader.last_error ? loader.last_error(ctx) : "unknown error";
                        error = fmt::format("BLE write failed at packet {}: {}", i, err);
                        spdlog::error("Niimbot BT: {}", error);
                        write_ok = false;
                        break;
                    }

                    // Inter-packet delay: 10ms for image rows, 100ms for commands
                    uint8_t cmd = pkt[2]; // command byte is at index 2
                    bool is_image = (cmd == static_cast<uint8_t>(NiimbotCmd::PrintBitmapRow) ||
                                     cmd == static_cast<uint8_t>(NiimbotCmd::PrintBitmapRowIndexed) ||
                                     cmd == static_cast<uint8_t>(NiimbotCmd::PrintEmptyRow));
                    int delay_ms = is_image ? 10 : 100;
                    std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
                }

                if (write_ok) {
                    success = true;
                    spdlog::info("Niimbot BT: all {} packets sent successfully",
                                 job.packets.size());
                    // Wait for printer to finish processing
                    std::this_thread::sleep_for(std::chrono::seconds(3));
                }

                loader.disconnect(ctx, handle);
            }
            loader.deinit(ctx);
        }

        helix::ui::queue_update([callback, success, error]() {
            if (callback) callback(success, error);
        });
    }).detach();
}

}  // namespace helix::label
