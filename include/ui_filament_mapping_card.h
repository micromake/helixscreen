// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "filament_mapper.h"
#include "ui_filament_picker_modal.h"

#include <lvgl.h>

#include <string>
#include <vector>

namespace helix::ui {

/**
 * @brief Filament mapping card for the print detail view
 *
 * Replaces the simple color swatches display with an interactive
 * mapping card that shows which AMS/toolchanger slot is assigned
 * to each G-code tool. Users can tap a row to open the picker modal
 * and reassign slots.
 *
 * Visibility: shown when AMS/toolchanger is detected AND the file
 * uses at least one tool. Hidden otherwise (falls back to nothing).
 */
class FilamentMappingCard {
  public:
    FilamentMappingCard() = default;
    ~FilamentMappingCard() = default;

    // Non-copyable (holds LVGL widget pointers)
    FilamentMappingCard(const FilamentMappingCard&) = delete;
    FilamentMappingCard& operator=(const FilamentMappingCard&) = delete;

    /**
     * @brief Attach to XML widgets after instantiation
     *
     * @param card_widget The filament_mapping_card ui_card
     * @param rows_container The filament_mapping_rows container
     * @param warning_container The filament_mapping_warning container
     */
    void create(lv_obj_t* card_widget, lv_obj_t* rows_container, lv_obj_t* warning_container);

    /**
     * @brief Update with new file data + current AMS state
     *
     * Shows the card if AMS is available and file has tools.
     * Computes default mappings via FilamentMapper::compute_defaults().
     *
     * @param gcode_colors Per-tool hex color strings (e.g., "#FF0000")
     * @param gcode_materials Per-tool material strings (e.g., "PLA")
     */
    void update(const std::vector<std::string>& gcode_colors,
                const std::vector<std::string>& gcode_materials);

    /**
     * @brief Hide the card
     */
    void hide();

    /**
     * @brief Get current tool-to-slot mappings
     */
    [[nodiscard]] std::vector<helix::ToolMapping> get_mappings() const { return mappings_; }

    /**
     * @brief Check if card is currently visible
     */
    [[nodiscard]] bool is_visible() const;

    /**
     * @brief Null widget pointers (called during destroy-on-close)
     */
    void on_ui_destroyed();

  private:
    void rebuild_rows();
    void update_warning_banner();
    void on_row_tapped(int tool_index);
    void on_slot_selected(int tool_index, const FilamentPickerModal::Selection& selection);

    /// Build AvailableSlot list from AmsState singleton
    std::vector<helix::AvailableSlot> collect_available_slots();

    /// Build GcodeToolInfo list from color/material strings
    std::vector<helix::GcodeToolInfo> build_tool_info(
        const std::vector<std::string>& colors,
        const std::vector<std::string>& materials);

    /// Create a single mapping row widget
    lv_obj_t* create_row(int tool_index);

    /// Get display text for a mapping
    std::string get_slot_display_text(const helix::ToolMapping& mapping) const;

    lv_obj_t* card_ = nullptr;
    lv_obj_t* rows_container_ = nullptr;
    lv_obj_t* warning_container_ = nullptr;

    std::vector<helix::ToolMapping> mappings_;
    std::vector<helix::GcodeToolInfo> tool_info_;
    std::vector<helix::AvailableSlot> available_slots_;

    FilamentPickerModal picker_modal_;
};

} // namespace helix::ui
