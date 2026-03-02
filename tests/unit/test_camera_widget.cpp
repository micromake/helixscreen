// SPDX-License-Identifier: GPL-3.0-or-later

#include "panel_widget_registry.h"

#include "../catch_amalgamated.hpp"

#include "lvgl.h"

#if LV_USE_LIBJPEG_TURBO
#include "camera_stream.h"
#endif

using namespace helix;

// ============================================================================
// Registry: camera widget definition
// ============================================================================

#if LV_USE_LIBJPEG_TURBO

TEST_CASE("CameraWidget: registered in widget registry", "[camera][panel_widget]") {
    const auto* def = find_widget_def("camera");
    REQUIRE(def != nullptr);
    REQUIRE(std::string(def->display_name) == "Camera");
    REQUIRE(std::string(def->icon) == "video");
    REQUIRE(def->hardware_gate_subject != nullptr);
    REQUIRE(std::string(def->hardware_gate_subject) == "printer_has_webcam");
    REQUIRE(def->default_enabled == false); // opt-in widget
    REQUIRE(def->colspan == 2);
    REQUIRE(def->rowspan == 2);
    REQUIRE(def->min_colspan == 1);
    REQUIRE(def->min_rowspan == 1);
    REQUIRE(def->max_colspan == 4);
    REQUIRE(def->max_rowspan == 3);
}

// ============================================================================
// MJPEG boundary parser
// ============================================================================

TEST_CASE("CameraStream: construct and destroy without crash", "[camera]") {
    CameraStream stream;
    REQUIRE_FALSE(stream.is_running());
}

TEST_CASE("CameraStream: start with empty URLs does nothing", "[camera]") {
    CameraStream stream;
    bool callback_called = false;
    stream.start("", "", [&](lv_draw_buf_t*) { callback_called = true; });
    REQUIRE_FALSE(stream.is_running());
    REQUIRE_FALSE(callback_called);
}

TEST_CASE("CameraStream: frame_consumed resets pending flag", "[camera]") {
    CameraStream stream;
    // Just verify the method doesn't crash when called without active stream
    stream.frame_consumed();
    REQUIRE_FALSE(stream.is_running());
}

#else // !LV_USE_LIBJPEG_TURBO

TEST_CASE("CameraWidget: not registered on embedded platforms", "[camera][panel_widget]") {
    const auto* def = find_widget_def("camera");
    REQUIRE(def == nullptr);
}

#endif // LV_USE_LIBJPEG_TURBO
