// SPDX-License-Identifier: GPL-3.0-or-later

#include "camera_widget.h"

#include "lvgl.h"

#if HELIX_HAS_CAMERA

#include "app_globals.h"
#include "moonraker_api.h"
#include "panel_widget_registry.h"
#include "printer_state.h"
#include "static_subject_registry.h"
#include "subject_debug_registry.h"
#include "ui_update_queue.h"

#include <spdlog/spdlog.h>

// Module-level subject for status text binding (static — shared across instances)
static lv_subject_t s_camera_status_subject;
static char s_camera_status_buffer[64];
static bool s_subjects_initialized = false;

static void camera_widget_init_subjects() {
    if (s_subjects_initialized) {
        return;
    }

    lv_subject_init_string(&s_camera_status_subject, s_camera_status_buffer, nullptr,
                           sizeof(s_camera_status_buffer), "No camera");
    lv_xml_register_subject(nullptr, "camera_status_text", &s_camera_status_subject);
    SubjectDebugRegistry::instance().register_subject(&s_camera_status_subject,
                                                      "camera_status_text",
                                                      LV_SUBJECT_TYPE_STRING, __FILE__, __LINE__);

    s_subjects_initialized = true;

    StaticSubjectRegistry::instance().register_deinit("CameraWidgetSubjects", []() {
        if (s_subjects_initialized && lv_is_initialized()) {
            lv_subject_deinit(&s_camera_status_subject);
            s_subjects_initialized = false;
            spdlog::trace("[CameraWidget] Subjects deinitialized");
        }
    });

    spdlog::debug("[CameraWidget] Subjects initialized");
}

namespace helix {
void register_camera_widget() {
    register_widget_factory("camera", []() { return std::make_unique<CameraWidget>(); });
    register_widget_subjects("camera", camera_widget_init_subjects);
}

CameraWidget::CameraWidget() : alive_(std::make_shared<bool>(true)) {}

CameraWidget::~CameraWidget() {
    detach();
}

void CameraWidget::attach(lv_obj_t* widget_obj, lv_obj_t* /*parent_screen*/) {
    widget_obj_ = widget_obj;
    camera_image_ = lv_obj_find_by_name(widget_obj_, "camera_image");

    lv_obj_set_user_data(widget_obj_, this);

    // Widget is only shown when printer_has_webcam > 0 (registry gate),
    // so if we're attached, a webcam is configured
    auto& state = get_printer_state();
    bool has_urls = !state.get_webcam_stream_url().empty() || !state.get_webcam_snapshot_url().empty();

    if (has_urls) {
        set_status_text("Connecting...");
    } else {
        set_status_text("No camera");
    }

    spdlog::debug("[CameraWidget] Attached (has_urls={})", has_urls);
}

void CameraWidget::detach() {
    *alive_ = false;
    stop_stream();

    if (camera_image_ && lv_is_initialized() && lv_obj_is_valid(camera_image_)) {
        lv_image_set_src(camera_image_, nullptr);
    }

    if (widget_obj_) {
        lv_obj_set_user_data(widget_obj_, nullptr);
        widget_obj_ = nullptr;
    }
    camera_image_ = nullptr;

    // Reset alive guard for potential reuse
    alive_ = std::make_shared<bool>(true);

    spdlog::debug("[CameraWidget] Detached");
}

void CameraWidget::on_activate() {
    start_stream();
}

void CameraWidget::on_deactivate() {
    stop_stream();
}

void CameraWidget::on_size_changed(int /*colspan*/, int /*rowspan*/, int /*width_px*/,
                                   int /*height_px*/) {
    // Image scales automatically via XML width="100%" height="100%"
    // If we need scale-to-cover, we'd adjust lv_image_set_inner_align here
    if (camera_image_) {
        lv_image_set_inner_align(camera_image_, LV_IMAGE_ALIGN_COVER);
    }
}

void CameraWidget::start_stream() {
    if (stream_ && stream_->is_running()) {
        return;
    }

    auto& state = get_printer_state();
    std::string stream_url = state.get_webcam_stream_url();
    std::string snapshot_url = state.get_webcam_snapshot_url();

    if (stream_url.empty() && snapshot_url.empty()) {
        set_status_text("No camera URL");
        return;
    }

    // Resolve relative URLs against the web frontend (nginx on port 80).
    // Moonraker's webcam URLs are relative paths meant for the nginx reverse
    // proxy, NOT the Moonraker API port (7125). Extract the host from the
    // Moonraker HTTP base URL and use port 80.
    auto* api = get_moonraker_api();
    if (api) {
        const auto& base = api->get_http_base_url();
        spdlog::debug("[CameraWidget] HTTP base URL: '{}'", base);
        if (!base.empty()) {
            // Extract host from base URL: "http://HOST:PORT" -> "http://HOST"
            std::string web_base;
            auto scheme_end = base.find("://");
            if (scheme_end != std::string::npos) {
                auto host_start = scheme_end + 3;
                auto port_pos = base.find(':', host_start);
                if (port_pos != std::string::npos) {
                    web_base = base.substr(0, port_pos); // "http://HOST"
                } else {
                    web_base = base; // Already no port
                }
            }
            if (web_base.empty()) {
                web_base = base; // Fallback
            }
            spdlog::debug("[CameraWidget] Web frontend base: '{}'", web_base);

            if (!stream_url.empty() && stream_url[0] == '/') {
                stream_url = web_base + stream_url;
            }
            if (!snapshot_url.empty() && snapshot_url[0] == '/') {
                snapshot_url = web_base + snapshot_url;
            }
        }
    } else {
        spdlog::warn("[CameraWidget] No MoonrakerAPI available for URL resolution");
    }

    set_status_text("Connecting...");

    stream_ = std::make_unique<CameraStream>();
    stream_->set_flip(state.get_webcam_flip_horizontal(), state.get_webcam_flip_vertical());

    // Capture alive guard by value for safe callback
    std::weak_ptr<bool> weak_alive = alive_;

    stream_->start(
        stream_url, snapshot_url,
        [this, weak_alive](lv_draw_buf_t* frame) {
            auto alive = weak_alive.lock();
            if (!alive || !*alive) return;

            helix::ui::queue_update([this, weak_alive, frame]() {
                auto alive = weak_alive.lock();
                if (!alive || !*alive) return;

                if (camera_image_) {
                    lv_image_set_src(camera_image_, frame);
                    set_status_text("");
                    // Hide spinner overlay on first frame
                    if (widget_obj_) {
                        lv_obj_t* overlay = lv_obj_find_by_name(widget_obj_, "camera_overlay");
                        if (overlay && !lv_obj_has_flag(overlay, LV_OBJ_FLAG_HIDDEN)) {
                            lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
                        }
                    }
                }

                if (stream_) stream_->frame_consumed();
            });
        },
        [this, weak_alive](const char* msg) {
            auto alive = weak_alive.lock();
            if (!alive || !*alive) return;

            std::string status(msg);
            helix::ui::queue_update([this, weak_alive, status]() {
                auto alive = weak_alive.lock();
                if (!alive || !*alive) return;
                set_status_text(status.c_str());
            });
        });

    spdlog::info("[CameraWidget] Stream started (stream={}, snapshot={})", stream_url, snapshot_url);
}

void CameraWidget::stop_stream() {
    if (stream_) {
        // Invalidate alive guard FIRST — queued UI callbacks check this and
        // become no-ops, preventing use-after-free on freed draw buffers
        *alive_ = false;

        // Clear image source before stopping — stop() frees the draw buffers
        // that LVGL may still reference for rendering
        if (camera_image_ && lv_is_initialized()) {
            lv_image_set_src(camera_image_, nullptr);
        }
        stream_->stop();
        stream_.reset();

        // Reset alive guard so the stream can be restarted (on_activate)
        alive_ = std::make_shared<bool>(true);

        spdlog::debug("[CameraWidget] Stream stopped");
    }
}

void CameraWidget::set_status_text(const char* text) {
    if (s_subjects_initialized) {
        lv_subject_copy_string(&s_camera_status_subject, text);
    }
}

} // namespace helix

#endif // HELIX_HAS_CAMERA
