// SPDX-License-Identifier: GPL-3.0-or-later

#include "../lvgl_test_fixture.h"
#include "config.h"
#include "display_settings_manager.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

// ============================================================================
// Screensaver Settings Tests
// ============================================================================

#ifdef HELIX_ENABLE_SCREENSAVER

TEST_CASE_METHOD(LVGLTestFixture, "Screensaver setting defaults to true when compiled in",
                 "[screensaver][display_settings]") {
    Config::get_instance();
    DisplaySettingsManager::instance().init_subjects();

    REQUIRE(DisplaySettingsManager::instance().get_screensaver_enabled() == true);

    DisplaySettingsManager::instance().deinit_subjects();
}

TEST_CASE_METHOD(LVGLTestFixture, "Screensaver setting set/get round trip",
                 "[screensaver][display_settings]") {
    Config::get_instance();
    DisplaySettingsManager::instance().init_subjects();

    SECTION("disable screensaver") {
        DisplaySettingsManager::instance().set_screensaver_enabled(false);
        REQUIRE(DisplaySettingsManager::instance().get_screensaver_enabled() == false);
    }

    SECTION("re-enable screensaver") {
        DisplaySettingsManager::instance().set_screensaver_enabled(false);
        DisplaySettingsManager::instance().set_screensaver_enabled(true);
        REQUIRE(DisplaySettingsManager::instance().get_screensaver_enabled() == true);
    }

    DisplaySettingsManager::instance().deinit_subjects();
}

TEST_CASE_METHOD(LVGLTestFixture, "Screensaver subject reflects setter",
                 "[screensaver][display_settings]") {
    Config::get_instance();
    DisplaySettingsManager::instance().init_subjects();

    DisplaySettingsManager::instance().set_screensaver_enabled(false);
    REQUIRE(lv_subject_get_int(DisplaySettingsManager::instance().subject_screensaver_enabled()) ==
            0);

    DisplaySettingsManager::instance().set_screensaver_enabled(true);
    REQUIRE(lv_subject_get_int(DisplaySettingsManager::instance().subject_screensaver_enabled()) ==
            1);

    DisplaySettingsManager::instance().deinit_subjects();
}

// ============================================================================
// FlyingToasterScreensaver Lifecycle Tests
// ============================================================================

#include "ui_screensaver.h"

TEST_CASE_METHOD(LVGLTestFixture, "FlyingToasterScreensaver starts inactive",
                 "[screensaver]") {
    REQUIRE(FlyingToasterScreensaver::instance().is_active() == false);
}

TEST_CASE_METHOD(LVGLTestFixture, "FlyingToasterScreensaver start/stop lifecycle",
                 "[screensaver]") {
    auto& ss = FlyingToasterScreensaver::instance();

    SECTION("start activates screensaver") {
        ss.start();
        REQUIRE(ss.is_active() == true);
        ss.stop();
    }

    SECTION("stop deactivates screensaver") {
        ss.start();
        ss.stop();
        REQUIRE(ss.is_active() == false);
    }

    SECTION("double start is safe") {
        ss.start();
        ss.start();
        REQUIRE(ss.is_active() == true);
        ss.stop();
    }

    SECTION("double stop is safe") {
        ss.start();
        ss.stop();
        ss.stop();
        REQUIRE(ss.is_active() == false);
    }

    SECTION("stop without start is safe") {
        ss.stop();
        REQUIRE(ss.is_active() == false);
    }
}

TEST_CASE_METHOD(LVGLTestFixture, "FlyingToasterScreensaver creates overlay on lv_layer_top",
                 "[screensaver]") {
    auto& ss = FlyingToasterScreensaver::instance();

    int children_before = lv_obj_get_child_count(lv_layer_top());
    ss.start();
    int children_after = lv_obj_get_child_count(lv_layer_top());
    REQUIRE(children_after > children_before);

    ss.stop();
    int children_final = lv_obj_get_child_count(lv_layer_top());
    REQUIRE(children_final == children_before);
}

#endif // HELIX_ENABLE_SCREENSAVER
