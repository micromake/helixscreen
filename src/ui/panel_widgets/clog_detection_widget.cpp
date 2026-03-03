// SPDX-License-Identifier: GPL-3.0-or-later

#include "clog_detection_widget.h"

#include "ams_state.h"
#include "clog_detection_config_modal.h"
#include "panel_widget_registry.h"
#include "ui_clog_meter.h"

#include <spdlog/spdlog.h>

namespace helix {
void register_clog_detection_widget() {
    register_widget_factory("clog_detection",
                            []() { return std::make_unique<ClogDetectionWidget>(); });
}
} // namespace helix

using namespace helix;

ClogDetectionWidget::~ClogDetectionWidget() {
    detach();
}

void ClogDetectionWidget::attach(lv_obj_t* widget_obj, lv_obj_t* /*parent_screen*/) {
    widget_obj_ = widget_obj;
    if (!widget_obj_)
        return;

    lv_obj_set_user_data(widget_obj_, this);

    clog_meter_ = std::make_unique<ui::UiClogMeter>(widget_obj_);
    clog_meter_->set_fill_mode(true);

    apply_config();
}

void ClogDetectionWidget::detach() {
    clog_meter_.reset();
    if (widget_obj_) {
        lv_obj_set_user_data(widget_obj_, nullptr);
        widget_obj_ = nullptr;
    }
}

void ClogDetectionWidget::on_size_changed(int /*colspan*/, int /*rowspan*/, int /*width_px*/,
                                          int /*height_px*/) {
    if (clog_meter_)
        clog_meter_->resize_arc();
}

bool ClogDetectionWidget::on_edit_configure() {
    spdlog::info("[ClogDetectionWidget] Configure requested - showing config modal");
    config_modal_ = std::make_unique<ClogDetectionConfigModal>(id(), panel_id());
    config_modal_->show(lv_screen_active());
    return false;
}

void ClogDetectionWidget::set_config(const nlohmann::json& config) {
    config_ = config;
}

void ClogDetectionWidget::apply_config() {
    int source = 0;
    int threshold = 0;
    if (config_.contains("source") && config_["source"].is_number_integer())
        source = config_["source"].get<int>();
    if (config_.contains("danger_threshold") && config_["danger_threshold"].is_number_integer())
        threshold = config_["danger_threshold"].get<int>();

    auto& ams = AmsState::instance();
    ams.set_source_override(source);
    ams.set_danger_threshold_override(threshold);

    spdlog::debug("[ClogDetectionWidget] Applied config: source={}, threshold={}", source, threshold);
}
