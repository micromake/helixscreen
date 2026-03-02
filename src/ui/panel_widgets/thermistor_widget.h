// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_observer_guard.h"

#include "panel_widget.h"

#include <memory>
#include <string>

namespace helix {

class PrinterState;

/// Home widget displaying a user-selected temperature sensor reading.
/// Click opens a context menu to choose which sensor to monitor.
/// Selection persists via PanelWidgetConfig per-widget config.
class ThermistorWidget : public PanelWidget {
  public:
    explicit ThermistorWidget(PrinterState& printer_state);
    ~ThermistorWidget() override;

    void set_config(const nlohmann::json& config) override;
    void attach(lv_obj_t* widget_obj, lv_obj_t* parent_screen) override;
    void detach() override;
    const char* id() const override {
        return "thermistor";
    }

    /// Called from static event callback
    void handle_clicked();

    /// Select a sensor by klipper_name, update display, save config
    void select_sensor(const std::string& klipper_name);

    // Static event callbacks (XML-registered)
    static void thermistor_clicked_cb(lv_event_t* e);
    static void thermistor_picker_backdrop_cb(lv_event_t* e);

  private:
    lv_obj_t* widget_obj_ = nullptr;
    lv_obj_t* parent_screen_ = nullptr;
    lv_obj_t* temp_label_ = nullptr;
    lv_obj_t* name_label_ = nullptr;

    std::string selected_sensor_; // klipper_name (e.g., "temperature_sensor mcu_temp")
    std::string display_name_;    // Pretty name for label
    ObserverGuard temp_observer_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(false);
    char temp_buffer_[16] = {};

    // Sensor picker context menu
    lv_obj_t* picker_backdrop_ = nullptr;

    void resolve_display_name();
    void on_temp_changed(int centidegrees);
    void update_display();
    void save_config();
    void show_sensor_picker();
    void dismiss_sensor_picker();

    // Static active instance for picker event routing
    static ThermistorWidget* s_active_picker_;
};

} // namespace helix
