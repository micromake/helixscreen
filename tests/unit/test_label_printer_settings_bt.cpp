// SPDX-License-Identifier: GPL-3.0-or-later

#include "../catch_amalgamated.hpp"
#include "../lvgl_test_fixture.h"
#include "label_printer_settings.h"

using namespace helix;

TEST_CASE("Label printer settings - bluetooth type", "[label][settings]") {
    LVGLTestFixture fixture;

    auto& settings = LabelPrinterSettingsManager::instance();
    settings.init_subjects();

    settings.set_printer_type("bluetooth");
    REQUIRE(settings.get_printer_type() == "bluetooth");
}

TEST_CASE("Label printer settings - bt_address persistence", "[label][settings]") {
    LVGLTestFixture fixture;

    auto& settings = LabelPrinterSettingsManager::instance();
    settings.init_subjects();

    settings.set_bt_address("AA:BB:CC:DD:EE:FF");
    REQUIRE(settings.get_bt_address() == "AA:BB:CC:DD:EE:FF");
}

TEST_CASE("Label printer settings - bt_transport persistence", "[label][settings]") {
    LVGLTestFixture fixture;

    auto& settings = LabelPrinterSettingsManager::instance();
    settings.init_subjects();

    settings.set_bt_transport("spp");
    REQUIRE(settings.get_bt_transport() == "spp");

    settings.set_bt_transport("ble");
    REQUIRE(settings.get_bt_transport() == "ble");
}

TEST_CASE("Label printer settings - BT configured check", "[label][settings]") {
    LVGLTestFixture fixture;

    auto& settings = LabelPrinterSettingsManager::instance();
    settings.init_subjects();

    settings.set_printer_type("bluetooth");

    settings.set_bt_address("");
    REQUIRE_FALSE(settings.is_configured());

    settings.set_bt_address("AA:BB:CC:DD:EE:FF");
    REQUIRE(settings.is_configured());
}
