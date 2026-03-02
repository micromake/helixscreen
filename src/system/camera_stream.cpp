// SPDX-License-Identifier: GPL-3.0-or-later

#include "camera_stream.h"

#include "lvgl.h"

#if HELIX_HAS_CAMERA

#include "hv/requests.h"
#include "spdlog/spdlog.h"
#include "stb_image.h"

#include <algorithm>
#include <chrono>
#include <cstring>

namespace helix {

// ============================================================================
// Construction / Destruction
// ============================================================================

CameraStream::~CameraStream() {
    stop();
}

// ============================================================================
// Public API
// ============================================================================

void CameraStream::start(const std::string& stream_url, const std::string& snapshot_url,
                          FrameCallback on_frame, ErrorCallback on_error) {
    if (running_.load()) {
        stop();
    }

    stream_url_ = stream_url;
    snapshot_url_ = snapshot_url;
    on_frame_ = std::move(on_frame);
    on_error_ = std::move(on_error);
    stream_fail_count_ = 0;
    frame_pending_.store(false);
    running_.store(true);

    spdlog::info("[CameraStream] Starting — stream={}, snapshot={}", stream_url_, snapshot_url_);

    // Use snapshot polling mode — libhv's sync HTTP API cannot handle the
    // never-ending MJPEG multipart response (blocks until timeout). Snapshot
    // mode fetches individual JPEG frames every kSnapshotIntervalMs.
    // TODO: Implement async/chunked HTTP for proper MJPEG streaming.
    if (!snapshot_url_.empty()) {
        spdlog::info("[CameraStream] Using snapshot mode (interval={}ms)", kSnapshotIntervalMs);
        stream_thread_ = std::thread(&CameraStream::snapshot_poll_loop, this);
    } else if (!stream_url_.empty()) {
        // No snapshot URL but have stream URL — try MJPEG as last resort
        stream_thread_ = std::thread(&CameraStream::stream_thread_func, this);
    } else {
        spdlog::warn("[CameraStream] No stream or snapshot URL provided");
        running_.store(false);
    }
}

void CameraStream::stop() {
    if (!running_.load()) {
        return;
    }

    spdlog::info("[CameraStream] Stopping");
    running_.store(false);

    if (stream_thread_.joinable()) {
        stream_thread_.join();
    }

    free_buffers();
    recv_buf_.clear();
    on_frame_ = nullptr;
    on_error_ = nullptr;
}

bool CameraStream::is_running() const {
    return running_.load();
}

void CameraStream::frame_consumed() {
    frame_pending_.store(false);
}

// ============================================================================
// MJPEG Stream Thread
// ============================================================================

void CameraStream::stream_thread_func() {
    spdlog::debug("[CameraStream] Stream thread started for {}", stream_url_);
    recv_buf_.clear();
    recv_buf_.reserve(256 * 1024);

    while (running_.load() && stream_fail_count_ < kMaxStreamFailures) {
        auto req = std::make_shared<HttpRequest>();
        req->method = HTTP_GET;
        req->url = stream_url_;
        req->timeout = kStreamTimeoutSec;

        auto resp = requests::request(req);

        // Check running_ after blocking HTTP call — stop() may have been called
        if (!running_.load()) {
            break;
        }

        if (!resp || resp->status_code < 200 || resp->status_code >= 300) {
            spdlog::warn("[CameraStream] Stream request failed (status={})",
                         resp ? static_cast<int>(resp->status_code) : -1);
            stream_fail_count_++;
            if (running_.load() && stream_fail_count_ < kMaxStreamFailures) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            continue;
        }

        // Parse boundary from Content-Type header
        std::string content_type = resp->GetHeader("Content-Type");
        std::string boundary;
        auto boundary_pos = content_type.find("boundary=");
        if (boundary_pos != std::string::npos) {
            boundary = content_type.substr(boundary_pos + 9);
            if (!boundary.empty() && boundary.front() == '"') {
                boundary = boundary.substr(1);
            }
            if (!boundary.empty() && boundary.back() == '"') {
                boundary.pop_back();
            }
            if (boundary.substr(0, 2) != "--") {
                boundary = "--" + boundary;
            }
        }

        if (boundary.empty()) {
            spdlog::warn("[CameraStream] No boundary in Content-Type: {}", content_type);
            // Try as a single JPEG (snapshot-style response)
            if (!resp->body.empty()) {
                auto* body_data = reinterpret_cast<const uint8_t*>(resp->body.data());
                if (decode_jpeg(body_data, resp->body.size())) {
                    deliver_frame();
                }
            }
            stream_fail_count_++;
            if (running_.load() && stream_fail_count_ < kMaxStreamFailures) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            continue;
        }

        // libhv's sync API reads the entire response body. For MJPEG we parse
        // whatever frames we got, then reconnect for fresh frames.
        spdlog::debug("[CameraStream] Got MJPEG response, boundary='{}', body={} bytes",
                      boundary, resp->body.size());

        recv_buf_.assign(resp->body.begin(), resp->body.end());
        parse_mjpeg_frames(boundary);

        // Reset fail count on success
        if (!resp->body.empty()) {
            stream_fail_count_ = 0;
        }

        // Brief pause before reconnecting
        if (running_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    // Fall back to snapshot mode if streaming failed
    if (running_.load() && stream_fail_count_ >= kMaxStreamFailures) {
        spdlog::warn("[CameraStream] Stream failed {} times, falling back to snapshot mode",
                     stream_fail_count_);
        if (!snapshot_url_.empty()) {
            if (on_error_) on_error_("Stream failed, trying snapshots...");
            snapshot_poll_loop();
        } else {
            if (on_error_) on_error_("Stream unavailable");
        }
    }

    spdlog::debug("[CameraStream] Stream thread exiting");
}

void CameraStream::parse_mjpeg_frames(const std::string& boundary) {
    size_t search_pos = 0;

    while (running_.load() && search_pos < recv_buf_.size()) {
        // Find boundary
        auto buf_str = std::string(reinterpret_cast<const char*>(recv_buf_.data() + search_pos),
                                   recv_buf_.size() - search_pos);
        auto bpos = buf_str.find(boundary);
        if (bpos == std::string::npos) {
            break;
        }

        // Find end of headers (double CRLF)
        auto header_end = buf_str.find("\r\n\r\n", bpos);
        if (header_end == std::string::npos) {
            break;
        }
        size_t jpeg_start = search_pos + header_end + 4;

        // Find next boundary to determine JPEG data end
        size_t remaining = recv_buf_.size() - jpeg_start;
        auto next_buf = std::string(reinterpret_cast<const char*>(recv_buf_.data() + jpeg_start),
                                    remaining);
        auto next_bpos = next_buf.find(boundary);

        size_t jpeg_len;
        if (next_bpos != std::string::npos) {
            jpeg_len = next_bpos;
            // Strip trailing CRLF
            while (jpeg_len > 0 &&
                   (next_buf[jpeg_len - 1] == '\r' || next_buf[jpeg_len - 1] == '\n')) {
                jpeg_len--;
            }
            search_pos = jpeg_start + next_bpos;
        } else {
            jpeg_len = remaining;
            while (jpeg_len > 0 && (recv_buf_[jpeg_start + jpeg_len - 1] == '\r' ||
                                    recv_buf_[jpeg_start + jpeg_len - 1] == '\n')) {
                jpeg_len--;
            }
            search_pos = recv_buf_.size();
        }

        if (jpeg_len == 0) {
            continue;
        }

        // Skip if UI hasn't consumed previous frame
        if (frame_pending_.load()) {
            spdlog::trace("[CameraStream] Skipping frame — previous not consumed");
            continue;
        }

        if (decode_jpeg(recv_buf_.data() + jpeg_start, jpeg_len)) {
            deliver_frame();
        }
    }
}

void CameraStream::snapshot_poll_loop() {
    spdlog::info("[CameraStream] Starting snapshot poll loop (interval={}ms)", kSnapshotIntervalMs);

    while (running_.load()) {
        if (!frame_pending_.load()) {
            fetch_snapshot();
        }
        // Sleep in small increments to check running_ flag
        for (int i = 0; i < kSnapshotIntervalMs / 100 && running_.load(); i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void CameraStream::fetch_snapshot() {
    if (snapshot_url_.empty()) {
        return;
    }

    spdlog::trace("[CameraStream] Fetching snapshot from {}", snapshot_url_);

    auto req = std::make_shared<HttpRequest>();
    req->method = HTTP_GET;
    req->url = snapshot_url_;
    req->timeout = kStreamTimeoutSec;

    auto resp = requests::request(req);

    // Check running_ after blocking HTTP call — stop() may have been called
    if (!running_.load()) {
        return;
    }

    if (!resp || resp->status_code < 200 || resp->status_code >= 300) {
        spdlog::debug("[CameraStream] Snapshot fetch failed (status={})",
                      resp ? static_cast<int>(resp->status_code) : -1);
        return;
    }

    if (resp->body.empty()) {
        return;
    }

    auto* data = reinterpret_cast<const uint8_t*>(resp->body.data());
    if (decode_jpeg(data, resp->body.size())) {
        deliver_frame();
    }
}

// ============================================================================
// JPEG Decode (using stb_image — no external dependency)
// ============================================================================

bool CameraStream::decode_jpeg(const uint8_t* data, size_t len) {
    if (!data || len == 0) {
        return false;
    }

    // Validate JPEG SOI marker
    if (len < 2 || data[0] != 0xFF || data[1] != 0xD8) {
        spdlog::debug("[CameraStream] Invalid JPEG data (no SOI marker)");
        return false;
    }

    int width = 0;
    int height = 0;
    int channels = 0;

    // Decode JPEG to RGB (3 channels)
    uint8_t* pixels = stbi_load_from_memory(data, static_cast<int>(len), &width, &height,
                                            &channels, 3);
    if (!pixels) {
        spdlog::debug("[CameraStream] JPEG decode failed: {}", stbi_failure_reason());
        return false;
    }

    if (width <= 0 || height <= 0 || width > 4096 || height > 4096) {
        spdlog::warn("[CameraStream] Invalid JPEG dimensions: {}x{}", width, height);
        stbi_image_free(pixels);
        return false;
    }

    ensure_buffers(width, height);

    if (!back_buf_) {
        stbi_image_free(pixels);
        return false;
    }

    // Apply flip transforms if configured, then copy into LVGL draw buffer
    auto* dst = static_cast<uint8_t*>(back_buf_->data);
    int dst_stride = static_cast<int>(back_buf_->header.stride);
    int src_stride = width * 3;

    // Load flip flags once (atomic) for consistency across the frame
    bool do_flip_h = flip_h_.load();
    bool do_flip_v = flip_v_.load();

    for (int y = 0; y < height; y++) {
        int src_y = do_flip_v ? (height - 1 - y) : y;
        const uint8_t* src_row = pixels + src_y * src_stride;
        uint8_t* dst_row = dst + y * dst_stride;

        if (do_flip_h) {
            for (int x = 0; x < width; x++) {
                int src_x = width - 1 - x;
                dst_row[x * 3 + 0] = src_row[src_x * 3 + 0];
                dst_row[x * 3 + 1] = src_row[src_x * 3 + 1];
                dst_row[x * 3 + 2] = src_row[src_x * 3 + 2];
            }
        } else {
            std::memcpy(dst_row, src_row, static_cast<size_t>(src_stride));
        }
    }

    stbi_image_free(pixels);
    spdlog::trace("[CameraStream] Decoded frame {}x{}", width, height);
    return true;
}

// ============================================================================
// Frame Delivery
// ============================================================================

void CameraStream::deliver_frame() {
    if (!on_frame_ || !back_buf_) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(buf_mutex_);
        std::swap(front_buf_, back_buf_);
    }

    frame_pending_.store(true);
    on_frame_(front_buf_);
}

// ============================================================================
// Buffer Management
// ============================================================================

// Allocate a draw buffer using the system allocator (calloc/free) instead of
// lv_draw_buf_create which calls lv_malloc — LVGL's allocator is NOT
// thread-safe and these run on the background stream thread.
lv_draw_buf_t* CameraStream::create_draw_buf(uint32_t w, uint32_t h, lv_color_format_t cf) {
    auto* buf = static_cast<lv_draw_buf_t*>(calloc(1, sizeof(lv_draw_buf_t)));
    if (!buf) return nullptr;

    uint32_t stride = lv_draw_buf_width_to_stride(w, cf);
    uint32_t data_size = stride * h;

    auto* data = calloc(1, data_size);
    if (!data) {
        free(buf);
        return nullptr;
    }

    lv_draw_buf_init(buf, w, h, cf, stride, data, data_size);
    lv_draw_buf_set_flag(buf, LV_IMAGE_FLAGS_MODIFIABLE);
    return buf;
}

void CameraStream::destroy_draw_buf(lv_draw_buf_t* buf) {
    if (!buf) return;
    free(buf->data);
    free(buf);
}

void CameraStream::ensure_buffers(int width, int height) {
    if (front_buf_ && frame_width_ == width && frame_height_ == height) {
        return;
    }

    spdlog::debug("[CameraStream] Allocating buffers for {}x{}", width, height);

    // Retire old front buffer — LVGL may still reference it via lv_image_set_src
    // until the widget processes the next frame and updates the source pointer.
    // Retired buffers are freed in free_buffers() after the thread joins.
    if (front_buf_) {
        retired_bufs_.push_back(front_buf_);
        front_buf_ = nullptr;
    }
    if (back_buf_) {
        destroy_draw_buf(back_buf_);
        back_buf_ = nullptr;
    }

    auto w = static_cast<uint32_t>(width);
    auto h = static_cast<uint32_t>(height);
    front_buf_ = create_draw_buf(w, h, LV_COLOR_FORMAT_RGB888);
    back_buf_ = create_draw_buf(w, h, LV_COLOR_FORMAT_RGB888);

    if (!front_buf_ || !back_buf_) {
        spdlog::error("[CameraStream] Failed to allocate draw buffers for {}x{}", width, height);
        destroy_draw_buf(front_buf_);
        destroy_draw_buf(back_buf_);
        front_buf_ = nullptr;
        back_buf_ = nullptr;
        return;
    }

    frame_width_ = width;
    frame_height_ = height;
}

void CameraStream::free_buffers() {
    std::lock_guard<std::mutex> lock(buf_mutex_);
    destroy_draw_buf(front_buf_);
    front_buf_ = nullptr;
    destroy_draw_buf(back_buf_);
    back_buf_ = nullptr;
    for (auto* buf : retired_bufs_) {
        destroy_draw_buf(buf);
    }
    retired_bufs_.clear();
    frame_width_ = 0;
    frame_height_ = 0;
}

} // namespace helix

#endif // HELIX_HAS_CAMERA
