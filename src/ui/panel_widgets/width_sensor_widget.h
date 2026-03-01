// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "panel_widget.h"

namespace helix {

/// Minimal widget class for width sensor — provides size-responsive scaling.
class WidthSensorWidget : public PanelWidget {
  public:
    WidthSensorWidget() = default;
    ~WidthSensorWidget() override;

    void attach(lv_obj_t* widget_obj, lv_obj_t* parent_screen) override;
    void detach() override;
    void on_size_changed(int colspan, int rowspan, int width_px, int height_px) override;
    const char* id() const override {
        return "width_sensor";
    }

  private:
    lv_obj_t* widget_obj_ = nullptr;
};

} // namespace helix
