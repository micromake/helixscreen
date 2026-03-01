// SPDX-License-Identifier: GPL-3.0-or-later

#include "width_sensor_widget.h"

#include "ui_fonts.h"

#include "panel_widget_registry.h"
#include "theme_manager.h"

#include <spdlog/spdlog.h>

namespace helix {
void register_width_sensor_widget() {
    register_widget_factory("width_sensor", []() { return std::make_unique<WidthSensorWidget>(); });
}
} // namespace helix

using namespace helix;

WidthSensorWidget::~WidthSensorWidget() {
    detach();
}

void WidthSensorWidget::attach(lv_obj_t* widget_obj, lv_obj_t* /*parent_screen*/) {
    widget_obj_ = widget_obj;
    if (widget_obj_)
        lv_obj_set_user_data(widget_obj_, this);
}

void WidthSensorWidget::detach() {
    if (widget_obj_) {
        lv_obj_set_user_data(widget_obj_, nullptr);
        widget_obj_ = nullptr;
    }
}

void WidthSensorWidget::on_size_changed(int colspan, int rowspan, int /*width_px*/,
                                        int /*height_px*/) {
    if (!widget_obj_)
        return;

    bool wide = (colspan >= 2);
    bool tall = (rowspan >= 2);

    // Scale icon when tall or wide
    const lv_font_t* icon_font = (tall || wide) ? &mdi_icons_32 : &mdi_icons_24;

    // Scale text when wide
    const char* font_token = wide ? "font_body" : "font_xs";
    const lv_font_t* text_font = theme_manager_get_font(font_token);
    if (!text_font)
        return;

    // Icon inside width_indicator
    lv_obj_t* indicator = lv_obj_find_by_name(widget_obj_, "width_indicator");
    if (indicator) {
        lv_obj_t* icon = lv_obj_get_child(indicator, 0);
        if (icon)
            lv_obj_set_style_text_font(icon, icon_font, 0);
    }

    // Diameter value label (named in width_indicator.xml)
    lv_obj_t* value_label = lv_obj_find_by_name(widget_obj_, "width_value");
    if (value_label)
        lv_obj_set_style_text_font(value_label, text_font, 0);

    // Bottom "Width" label — second child of the widget view
    uint32_t wcount = lv_obj_get_child_count(widget_obj_);
    if (wcount >= 2) {
        lv_obj_t* label = lv_obj_get_child(widget_obj_, 1);
        if (label)
            lv_obj_set_style_text_font(label, text_font, 0);
    }
}
