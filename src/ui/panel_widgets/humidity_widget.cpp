// SPDX-License-Identifier: GPL-3.0-or-later

#include "humidity_widget.h"

#include "ui_fonts.h"

#include "panel_widget_registry.h"
#include "theme_manager.h"

#include <spdlog/spdlog.h>

namespace helix {
void register_humidity_widget() {
    register_widget_factory("humidity", []() { return std::make_unique<HumidityWidget>(); });
}
} // namespace helix

using namespace helix;

HumidityWidget::~HumidityWidget() {
    detach();
}

void HumidityWidget::attach(lv_obj_t* widget_obj, lv_obj_t* /*parent_screen*/) {
    widget_obj_ = widget_obj;
    if (widget_obj_)
        lv_obj_set_user_data(widget_obj_, this);
}

void HumidityWidget::detach() {
    if (widget_obj_) {
        lv_obj_set_user_data(widget_obj_, nullptr);
        widget_obj_ = nullptr;
    }
}

void HumidityWidget::on_size_changed(int colspan, int rowspan, int /*width_px*/,
                                     int /*height_px*/) {
    if (!widget_obj_)
        return;

    bool wide = (colspan >= 2);
    bool tall = (rowspan >= 2);

    // Scale icon when tall or wide
    const lv_font_t* icon_font = (tall || wide) ? &mdi_icons_32 : &mdi_icons_24;

    // Scale text when wide
    const char* label_token = wide ? "font_body" : "font_xs";
    const char* value_token = wide ? "font_body" : "font_xs";
    const lv_font_t* label_font = theme_manager_get_font(label_token);
    const lv_font_t* value_font = theme_manager_get_font(value_token);
    if (!label_font || !value_font)
        return;

    // Icon inside humidity_indicator
    lv_obj_t* indicator = lv_obj_find_by_name(widget_obj_, "humidity_indicator");
    if (indicator) {
        // First child of indicator is the icon (lv_label with MDI font)
        lv_obj_t* icon = lv_obj_get_child(indicator, 0);
        if (icon)
            lv_obj_set_style_text_font(icon, icon_font, 0);
    }

    // Percentage value label (named in humidity_indicator.xml)
    lv_obj_t* value_label = lv_obj_find_by_name(widget_obj_, "humidity_value");
    if (value_label)
        lv_obj_set_style_text_font(value_label, value_font, 0);

    // Bottom "Humidity" label — second child of the widget view
    uint32_t wcount = lv_obj_get_child_count(widget_obj_);
    if (wcount >= 2) {
        lv_obj_t* label = lv_obj_get_child(widget_obj_, 1);
        if (label)
            lv_obj_set_style_text_font(label, label_font, 0);
    }
}
