// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "panel_widget.h"

#include <memory>
#include <string>

class ClogDetectionConfigModal;

namespace helix {
namespace ui {
class UiClogMeter;
}

/// Panel widget for clog/flow detection arc meter on the home panel.
class ClogDetectionWidget : public PanelWidget {
  public:
    ClogDetectionWidget() = default;
    ~ClogDetectionWidget() override;

    void set_config(const nlohmann::json& config) override;
    void attach(lv_obj_t* widget_obj, lv_obj_t* parent_screen) override;
    void detach() override;
    void on_size_changed(int colspan, int rowspan, int width_px, int height_px) override;
    const char* id() const override {
        return "clog_detection";
    }

    bool has_edit_configure() const override { return true; }
    bool on_edit_configure() override;

  private:
    void apply_config();

    nlohmann::json config_;
    lv_obj_t* widget_obj_ = nullptr;
    std::unique_ptr<ui::UiClogMeter> clog_meter_;
    std::unique_ptr<ClogDetectionConfigModal> config_modal_;
};

} // namespace helix
