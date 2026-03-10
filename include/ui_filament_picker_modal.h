// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "filament_mapper.h"
#include "ui_modal.h"

#include <functional>
#include <string>
#include <vector>

namespace helix::ui {

/**
 * @brief Modal for selecting which AMS slot to assign to a G-code tool
 *
 * Shows a scrollable list of available AMS/toolchanger slots with color
 * swatches and material info. Users can select a slot or choose "Auto"
 * to let the firmware decide.
 *
 * Usage:
 *   picker_.set_tool_info(0, 0xFF0000, "PLA");
 *   picker_.set_available_slots(slots);
 *   picker_.set_on_select([this](auto& sel) { handle_selection(sel); });
 *   picker_.show(lv_screen_active());
 */
class FilamentPickerModal : public Modal {
  public:
    struct Selection {
        int slot_index = -1;
        int backend_index = -1;
        bool is_auto = false;
    };

    using SelectCallback = std::function<void(const Selection&)>;

    const char* get_name() const override { return "Filament Picker"; }
    const char* component_name() const override { return "filament_picker_modal"; }

    /// Configure which tool we're picking for
    void set_tool_info(int tool_index, uint32_t expected_color,
                       const std::string& expected_material);

    /// Set the available slots to display
    void set_available_slots(const std::vector<helix::AvailableSlot>& slots);

    /// Set the current selection (for highlighting)
    void set_current_selection(int slot_index, int backend_index);

    /// Set callback for when user confirms a selection
    void set_on_select(SelectCallback cb);

  protected:
    void on_show() override;
    void on_ok() override;
    void on_cancel() override;

  private:
    void populate_slot_list();
    lv_obj_t* create_slot_row(lv_obj_t* parent, int index, const helix::AvailableSlot& slot);
    lv_obj_t* create_auto_row(lv_obj_t* parent);
    void select_row(int slot_index, int backend_index, bool is_auto);
    void update_row_highlights();

    /// Create a color swatch circle
    static lv_obj_t* create_color_swatch(lv_obj_t* parent, uint32_t color_rgb, lv_coord_t size);

    SelectCallback on_select_cb_;
    int tool_index_ = -1;
    uint32_t expected_color_ = 0;
    std::string expected_material_;
    std::vector<helix::AvailableSlot> slots_;
    Selection current_selection_;

    lv_obj_t* slot_list_ = nullptr;
};

} // namespace helix::ui
