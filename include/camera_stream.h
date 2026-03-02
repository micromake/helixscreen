// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "lvgl.h"

// LV_USE_LIBJPEG_TURBO is used as a platform gate — camera features are only
// available on Pi/desktop platforms. Actual JPEG decoding uses stb_image.
#if LV_USE_LIBJPEG_TURBO

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace helix {

/**
 * @brief MJPEG camera stream decoder with snapshot fallback
 *
 * Connects to an MJPEG stream URL, parses multipart boundaries, decodes
 * JPEG frames via stb_image, and delivers decoded RGB888 frames via callback.
 * Falls back to periodic snapshot polling if streaming fails.
 *
 * Threading: decode happens on a background thread. The on_frame callback is
 * called from that thread — callers must use ui_queue_update() to marshal to
 * the LVGL thread.
 */
class CameraStream {
  public:
    using FrameCallback = std::function<void(lv_draw_buf_t* frame)>;
    using ErrorCallback = std::function<void(const char* message)>;

    CameraStream() = default;
    ~CameraStream();

    CameraStream(const CameraStream&) = delete;
    CameraStream& operator=(const CameraStream&) = delete;

    void set_flip(bool horizontal, bool vertical) {
        flip_h_.store(horizontal);
        flip_v_.store(vertical);
    }

    void start(const std::string& stream_url, const std::string& snapshot_url,
               FrameCallback on_frame, ErrorCallback on_error = nullptr);
    void stop();
    bool is_running() const;

    /// Called by widget when it has consumed the current frame
    void frame_consumed();

  private:
    void stream_thread_func();
    void parse_mjpeg_frames(const std::string& boundary);
    void snapshot_poll_loop();
    void fetch_snapshot();
    bool decode_jpeg(const uint8_t* data, size_t len);
    void deliver_frame();
    void ensure_buffers(int width, int height);
    void free_buffers();

    std::string stream_url_;
    std::string snapshot_url_;
    FrameCallback on_frame_;
    ErrorCallback on_error_;
    std::atomic<bool> flip_h_{false};
    std::atomic<bool> flip_v_{false};

    // Double buffer — decode into back, swap to front on delivery
    lv_draw_buf_t* front_buf_ = nullptr;
    lv_draw_buf_t* back_buf_ = nullptr;
    int frame_width_ = 0;
    int frame_height_ = 0;
    std::mutex buf_mutex_;

    // MJPEG parser state
    std::vector<uint8_t> recv_buf_;

    // State
    std::atomic<bool> running_{false};
    std::atomic<bool> frame_pending_{false};
    int stream_fail_count_ = 0;
    std::thread stream_thread_;

    static constexpr int kMaxStreamFailures = 3;
    static constexpr int kSnapshotIntervalMs = 2000;
    static constexpr int kStreamTimeoutSec = 10;
};

} // namespace helix

#endif // LV_USE_LIBJPEG_TURBO
