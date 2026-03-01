# Widget Sizing Constraints & Size Feedback

## Problem

Most panel widgets are hardcoded as non-scalable 1×1. The min/max constraint fields on `PanelWidgetDef` exist but are only populated for 3 widgets (printer_image, print_status, tips). Users cannot resize most widgets in edit mode, and widgets have no way to adapt their content to their allocated space.

## Design

### 1. Populate min/max for all widgets

Update `register_*_widget()` calls in `panel_widget_registry.cpp` with per-widget sizing constraints:

| Widget | Default | Min | Max | Notes |
|--------|---------|-----|-----|-------|
| printer_image | 2×2 | 1×1 | 4×3 | Already defined |
| print_status | 2×2 | 2×1 | 4×3 | Already defined |
| tips | 3×1 | 2×1 | 6×1 | Already defined |
| temperature | 1×1 | 1×1 | 2×2 | Mini graph at 2×2 |
| fan_stack | 2×1 | 1×1 | 3×2 | Fan list grows with space |
| led | 1×1 | 1×1 | 2×1 | Brightness bar at 2×1 |
| network | 1×1 | 1×1 | 2×1 | IP address at wider size |
| power | 1×1 | 1×1 | 1×1 | Just a button |
| ams | 1×1 | 1×1 | 2×2 | Multi-slot view |
| temp_stack | 1×1 | 1×1 | 3×2 | Multi-extruder list |
| filament | 1×1 | 1×1 | 2×1 | More info at wider size |
| probe | 1×1 | 1×1 | 2×1 | Value + label |
| humidity | 1×1 | 1×1 | 2×1 | Value + label |
| width_sensor | 1×1 | 1×1 | 2×1 | Value + label |
| thermistor | 1×1 | 1×1 | 2×1 | Value + label |
| firmware_restart | 1×1 | 1×1 | 1×1 | Just a button |
| notifications | 1×1 | 1×1 | 2×1 | Count + label |
| favorite_macro_1 | 1×1 | 1×1 | 2×1 | Macro name text |
| favorite_macro_2 | 1×1 | 1×1 | 2×1 | Macro name text |

Constraint approach: min/max per axis independently. Widgets center their content inside the allocated cell if the shape doesn't suit them.

### 2. Size feedback to widgets

Add a virtual method to `PanelWidget`:

```cpp
/// Called after attach and whenever the widget's grid allocation changes.
/// Widgets can adapt their content (show/hide elements, change layout).
virtual void on_size_changed(int colspan, int rowspan, int width_px, int height_px) {}
```

Called by `PanelWidgetManager` after `lv_obj_set_grid_cell()` and by `GridEditMode` after resize commits. Provides both grid span and actual pixel dimensions.

### 3. Content centering

Already handled by `LV_GRID_ALIGN_STRETCH` on placement. Widgets that want centered content use their XML flex layout. No code change needed.

## Implementation

1. Add `on_size_changed()` virtual to `PanelWidget` base class
2. Call it from `PanelWidgetManager::populate_widgets()` after grid cell placement
3. Call it from `GridEditMode::handle_resize_end()` after resize commit
4. Populate min/max values in `panel_widget_registry.cpp` for all widgets
5. Add tests for clamp_span with the new min/max values
