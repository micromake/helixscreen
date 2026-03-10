// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_filament_mapping_card.h"

#include "ams_state.h"
#include "theme_manager.h"
#include "ui_utils.h"

#include "lvgl/src/others/translation/lv_translation.h"

#include <spdlog/spdlog.h>

namespace helix::ui {

// Layout constants for dynamically created rows.
// Rows are built in C++ (not XML components) because the number of rows
// varies per file. lv_obj_add_event_cb and lv_label_set_text are used here
// as allowed exceptions for dynamic content.
namespace {
constexpr int ROW_PAD = 6;
constexpr int ROW_GAP = 8;
constexpr int SWATCH_SIZE = 16;
constexpr int SMALL_SWATCH_SIZE = 14;
constexpr int SELECTOR_PAD = 6;
constexpr int SELECTOR_RADIUS = 6;
constexpr int TOOL_LABEL_MIN_W = 24;
constexpr lv_opa_t SWATCH_BORDER_OPA = 30;
} // namespace

// ============================================================================
// Setup
// ============================================================================

void FilamentMappingCard::create(lv_obj_t* card_widget, lv_obj_t* rows_container,
                                  lv_obj_t* warning_container) {
    card_ = card_widget;
    rows_container_ = rows_container;
    warning_container_ = warning_container;

    spdlog::debug("[FilamentMapping] Card created");
}

// ============================================================================
// Update / visibility
// ============================================================================

void FilamentMappingCard::update(const std::vector<std::string>& gcode_colors,
                                  const std::vector<std::string>& gcode_materials) {
    if (!card_ || !rows_container_) {
        return;
    }

    // Check if AMS is available
    auto& ams = AmsState::instance();
    if (!ams.is_available()) {
        hide();
        return;
    }

    // Build tool info from file metadata
    tool_info_ = build_tool_info(gcode_colors, gcode_materials);

    if (tool_info_.empty()) {
        hide();
        return;
    }

    // Collect available slots from AMS backends
    available_slots_ = collect_available_slots();

    // Compute default mappings
    mappings_ = helix::FilamentMapper::compute_defaults(tool_info_, available_slots_);

    // Build the UI
    rebuild_rows();
    update_warning_banner();

    // Show the card
    lv_obj_remove_flag(card_, LV_OBJ_FLAG_HIDDEN);

    spdlog::debug("[FilamentMapping] Updated: {} tools, {} slots, {} mappings",
                  tool_info_.size(), available_slots_.size(), mappings_.size());
}

void FilamentMappingCard::hide() {
    if (card_) {
        lv_obj_add_flag(card_, LV_OBJ_FLAG_HIDDEN);
    }
}

bool FilamentMappingCard::is_visible() const {
    if (!card_) {
        return false;
    }
    return !lv_obj_has_flag(card_, LV_OBJ_FLAG_HIDDEN);
}

void FilamentMappingCard::on_ui_destroyed() {
    card_ = nullptr;
    rows_container_ = nullptr;
    warning_container_ = nullptr;
}

// ============================================================================
// Row building
// ============================================================================

void FilamentMappingCard::rebuild_rows() {
    if (!rows_container_) {
        return;
    }

    lv_obj_clean(rows_container_);

    for (size_t i = 0; i < mappings_.size(); ++i) {
        create_row(static_cast<int>(i));
    }
}

lv_obj_t* FilamentMappingCard::create_row(int tool_index) {
    if (!rows_container_ || tool_index < 0 ||
        tool_index >= static_cast<int>(mappings_.size())) {
        return nullptr;
    }

    const auto& mapping = mappings_[static_cast<size_t>(tool_index)];
    const auto& tool = tool_info_[static_cast<size_t>(tool_index)];

    // Row container
    lv_obj_t* row = lv_obj_create(rows_container_);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(row, ROW_PAD, 0);
    lv_obj_set_style_pad_gap(row, ROW_GAP, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_flex_cross_place(row, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    // Left side: tool label + expected color swatch
    bool show_tool_label = tool_info_.size() > 1;

    if (show_tool_label) {
        lv_obj_t* tool_label = lv_label_create(row);
        char tool_buf[8];
        snprintf(tool_buf, sizeof(tool_buf), "T%d", tool_index);
        lv_label_set_text(tool_label, tool_buf);
        lv_obj_set_style_text_font(tool_label, theme_manager_get_font("font_small"), 0);
        lv_obj_set_style_text_color(tool_label, theme_manager_get_color("text_muted"), 0);
        lv_obj_set_style_min_width(tool_label, TOOL_LABEL_MIN_W, 0);
    }

    // Expected color swatch (from gcode)
    lv_obj_t* expected_swatch = lv_obj_create(row);
    lv_obj_remove_style_all(expected_swatch);
    lv_obj_set_size(expected_swatch, SWATCH_SIZE, SWATCH_SIZE);
    lv_obj_set_style_radius(expected_swatch, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(expected_swatch, lv_color_hex(tool.color_rgb), 0);
    lv_obj_set_style_bg_opa(expected_swatch, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(expected_swatch, 1, 0);
    lv_obj_set_style_border_color(expected_swatch, theme_manager_get_color("text_muted"), 0);
    lv_obj_set_style_border_opa(expected_swatch, SWATCH_BORDER_OPA, 0);
    lv_obj_remove_flag(expected_swatch, LV_OBJ_FLAG_SCROLLABLE);

    // Right side: tappable slot selector
    lv_obj_t* selector = lv_obj_create(row);
    lv_obj_remove_style_all(selector);
    lv_obj_set_flex_grow(selector, 1);
    lv_obj_set_height(selector, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(selector, SELECTOR_PAD, 0);
    lv_obj_set_style_pad_gap(selector, SELECTOR_PAD, 0);
    lv_obj_set_flex_flow(selector, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_flex_cross_place(selector, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_radius(selector, SELECTOR_RADIUS, 0);
    lv_obj_set_style_bg_color(selector, theme_manager_get_color("elevated_bg"), 0);
    lv_obj_set_style_bg_opa(selector, LV_OPA_COVER, 0);
    lv_obj_add_flag(selector, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(selector, LV_OBJ_FLAG_SCROLLABLE);

    // Slot color swatch (mapped slot color)
    if (!mapping.is_auto && mapping.mapped_slot >= 0) {
        uint32_t slot_color = 0x808080;
        for (const auto& s : available_slots_) {
            if (s.slot_index == mapping.mapped_slot &&
                s.backend_index == mapping.mapped_backend) {
                slot_color = s.color_rgb;
                break;
            }
        }
        lv_obj_t* slot_swatch = lv_obj_create(selector);
        lv_obj_remove_style_all(slot_swatch);
        lv_obj_set_size(slot_swatch, SMALL_SWATCH_SIZE, SMALL_SWATCH_SIZE);
        lv_obj_set_style_radius(slot_swatch, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(slot_swatch, lv_color_hex(slot_color), 0);
        lv_obj_set_style_bg_opa(slot_swatch, LV_OPA_COVER, 0);
        lv_obj_remove_flag(slot_swatch, LV_OBJ_FLAG_SCROLLABLE);
    }

    // Slot text
    lv_obj_t* slot_text = lv_label_create(selector);
    std::string display = get_slot_display_text(mapping);
    lv_label_set_text(slot_text, display.c_str());
    lv_obj_set_style_text_font(slot_text, theme_manager_get_font("font_small"), 0);
    lv_obj_set_style_text_color(slot_text, theme_manager_get_color("text"), 0);
    lv_obj_set_flex_grow(slot_text, 1);

    // Chevron
    lv_obj_t* chevron = lv_label_create(selector);
    lv_label_set_text(chevron, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(chevron, theme_manager_get_color("text_muted"), 0);

    // Click handler (dynamic content exception — rows are rebuilt on each update)
    lv_obj_add_event_cb(
        selector,
        [](lv_event_t* e) {
            auto* self = static_cast<FilamentMappingCard*>(lv_event_get_user_data(e));
            lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
            int idx = static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(target)));
            self->on_row_tapped(idx);
        },
        LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(selector, reinterpret_cast<void*>(static_cast<intptr_t>(tool_index)));

    // Material mismatch indicator
    if (mapping.material_mismatch) {
        lv_obj_t* warn = lv_label_create(row);
        lv_label_set_text(warn, LV_SYMBOL_WARNING);
        lv_obj_set_style_text_color(warn, theme_manager_get_color("warning_color"), 0);
    }

    return row;
}

// ============================================================================
// Display text helpers
// ============================================================================

std::string FilamentMappingCard::get_slot_display_text(const helix::ToolMapping& mapping) const {
    if (mapping.is_auto) {
        return lv_tr("Auto");
    }

    if (mapping.mapped_slot < 0) {
        return lv_tr("Unmapped");
    }

    // Find the slot info
    for (const auto& slot : available_slots_) {
        if (slot.slot_index == mapping.mapped_slot &&
            slot.backend_index == mapping.mapped_backend) {
            char buf[64];
            if (slot.material.empty()) {
                snprintf(buf, sizeof(buf), "%s %d",
                         lv_tr("Slot"), slot.slot_index + 1);
            } else {
                snprintf(buf, sizeof(buf), "%s %d: %s",
                         lv_tr("Slot"), slot.slot_index + 1, slot.material.c_str());
            }
            return buf;
        }
    }

    char buf[32];
    snprintf(buf, sizeof(buf), "%s %d", lv_tr("Slot"), mapping.mapped_slot + 1);
    return buf;
}

// ============================================================================
// Warning banner — per-tool mismatch details
// ============================================================================

void FilamentMappingCard::update_warning_banner() {
    if (!warning_container_) {
        return;
    }

    // Collect per-tool mismatch descriptions
    std::vector<std::string> mismatches;
    for (size_t i = 0; i < mappings_.size(); ++i) {
        const auto& m = mappings_[i];
        if (!m.material_mismatch || m.mapped_slot < 0) {
            continue;
        }
        const auto& tool = tool_info_[i];
        // Find the mapped slot's material
        for (const auto& slot : available_slots_) {
            if (slot.slot_index == m.mapped_slot &&
                slot.backend_index == m.mapped_backend) {
                char buf[128];
                snprintf(buf, sizeof(buf), "T%d: %s %s, %s %d %s %s",
                         m.tool_index,
                         lv_tr("expects"), tool.material.c_str(),
                         lv_tr("Slot"), slot.slot_index + 1,
                         lv_tr("has"), slot.material.c_str());
                mismatches.push_back(buf);
                break;
            }
        }
    }

    if (mismatches.empty()) {
        lv_obj_add_flag(warning_container_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_remove_flag(warning_container_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clean(warning_container_);

    for (const auto& msg : mismatches) {
        lv_obj_t* warn_label = lv_label_create(warning_container_);
        lv_label_set_text(warn_label, msg.c_str());
        lv_obj_set_width(warn_label, LV_PCT(100));
        lv_obj_set_style_text_font(warn_label, theme_manager_get_font("font_small"), 0);
        lv_obj_set_style_text_color(warn_label, theme_manager_get_color("warning_color"), 0);
        lv_label_set_long_mode(warn_label, LV_LABEL_LONG_WRAP);
    }
}

// ============================================================================
// Row tap -> picker modal
// ============================================================================

void FilamentMappingCard::on_row_tapped(int tool_index) {
    if (tool_index < 0 || tool_index >= static_cast<int>(tool_info_.size())) {
        return;
    }

    const auto& tool = tool_info_[static_cast<size_t>(tool_index)];
    const auto& mapping = mappings_[static_cast<size_t>(tool_index)];

    spdlog::debug("[FilamentMapping] Row tapped: T{}", tool_index);

    picker_modal_.set_tool_info(tool_index, tool.color_rgb, tool.material);
    picker_modal_.set_available_slots(available_slots_);
    picker_modal_.set_current_selection(mapping.mapped_slot, mapping.mapped_backend);
    picker_modal_.set_on_select([this, tool_index](const FilamentPickerModal::Selection& sel) {
        on_slot_selected(tool_index, sel);
    });
    picker_modal_.show(lv_screen_active());
}

void FilamentMappingCard::on_slot_selected(int tool_index,
                                            const FilamentPickerModal::Selection& selection) {
    if (tool_index < 0 || tool_index >= static_cast<int>(mappings_.size())) {
        return;
    }

    auto& mapping = mappings_[static_cast<size_t>(tool_index)];
    mapping.mapped_slot = selection.slot_index;
    mapping.mapped_backend = selection.backend_index;
    mapping.is_auto = selection.is_auto;

    if (selection.is_auto) {
        mapping.reason = helix::ToolMapping::MatchReason::AUTO;
        mapping.material_mismatch = false;
    } else {
        // Check material mismatch
        mapping.material_mismatch = false;
        const auto& tool = tool_info_[static_cast<size_t>(tool_index)];
        if (!tool.material.empty()) {
            for (const auto& slot : available_slots_) {
                if (slot.slot_index == selection.slot_index &&
                    slot.backend_index == selection.backend_index) {
                    if (!slot.material.empty() &&
                        !helix::FilamentMapper::materials_match(tool.material, slot.material)) {
                        mapping.material_mismatch = true;
                    }
                    break;
                }
            }
        }
    }

    spdlog::info("[FilamentMapping] T{} mapped to: auto={}, slot={}, backend={}",
                 tool_index, selection.is_auto, selection.slot_index, selection.backend_index);

    // Rebuild UI to reflect changes
    rebuild_rows();
    update_warning_banner();
}

// ============================================================================
// Data collection
// ============================================================================

std::vector<helix::AvailableSlot> FilamentMappingCard::collect_available_slots() {
    std::vector<helix::AvailableSlot> slots;
    auto& ams = AmsState::instance();

    for (int bi = 0; bi < ams.backend_count(); ++bi) {
        auto* backend = ams.get_backend(bi);
        if (!backend) {
            continue;
        }

        auto info = backend->get_system_info();
        for (const auto& unit : info.units) {
            for (const auto& slot_info : unit.slots) {
                helix::AvailableSlot as;
                as.slot_index = slot_info.slot_index;
                as.backend_index = bi;
                as.color_rgb = slot_info.color_rgb;
                as.material = slot_info.material;
                as.is_empty = (slot_info.status == SlotStatus::EMPTY ||
                               slot_info.status == SlotStatus::UNKNOWN);
                as.current_tool_mapping = slot_info.mapped_tool;
                slots.push_back(std::move(as));
            }
        }
    }

    spdlog::debug("[FilamentMapping] Collected {} available slots from {} backends",
                  slots.size(), ams.backend_count());
    return slots;
}

std::vector<helix::GcodeToolInfo> FilamentMappingCard::build_tool_info(
    const std::vector<std::string>& colors,
    const std::vector<std::string>& materials) {
    std::vector<helix::GcodeToolInfo> tools;

    // Use the larger of colors or materials to determine tool count.
    // If both are empty, return empty — the card will be hidden.
    size_t count = std::max(colors.size(), materials.size());
    if (count == 0) {
        return tools;
    }

    for (size_t i = 0; i < count; ++i) {
        helix::GcodeToolInfo tool;
        tool.tool_index = static_cast<int>(i);

        // Parse color
        if (i < colors.size() && !colors[i].empty()) {
            auto parsed = ui_parse_hex_color(colors[i]);
            tool.color_rgb = parsed.value_or(0x808080);
        } else {
            tool.color_rgb = 0x808080;
        }

        // Material
        if (i < materials.size()) {
            tool.material = materials[i];
        }

        tools.push_back(std::move(tool));
    }

    return tools;
}

} // namespace helix::ui
