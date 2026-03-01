# Grid Edit Mode Design

## Overview

Android/Samsung-style in-panel grid editing for the home panel dashboard. Users long-press to enter edit mode, then drag widgets to reposition, resize via corner handles, add from a catalog overlay, and remove with an (X) button.

## Entry & Exit

- **Enter**: Long-press any widget or empty grid area on the home panel.
- **Exit**: Checkmark icon at the **top of the left navbar**. Tap to save & exit. Back/escape also exits and saves.
- **Reset to Default**: Accessible via long-press on the checkmark or a secondary icon.

## Visual Language

### Grid Dots
- Small dots at cell intersection points (Samsung-style).
- Color: `text_secondary` at ~30% opacity.
- Fade in when entering edit mode, fade out when exiting.

### Selected Widget
- L-shaped corner brackets at all 4 corners (accent color).
- **Resizable widgets** (where `PanelWidgetDef` min/max span differ): corner brackets become larger filled drag handles.
- **Non-resizable widgets**: brackets are decorative only.
- (X) button appears in the top-right corner of the selected widget for removal.

### Drag Feedback
- Widget lifts slightly on drag start (shadow + scale ~1.02x) — same elevation pattern as existing drag-to-reorder in `ui_settings_panel_widgets.cpp`.
- Ghost outline remains at original position (dashed border).
- Translucent preview snaps to nearest valid grid position while dragging.
- Invalid positions (occupied by different-size widget, out of bounds) don't show preview.
- On release: widget animates to new grid position.

### Resize Feedback
- Dragging a corner handle extends/shrinks the widget's span.
- Clamped to min/max colspan/rowspan from `PanelWidgetDef`.
- Corner outline stretches in real-time to show the new size.
- On release: widget snaps to new size.

### Empty Grid Areas
- No explicit empty cell widgets — the dot grid implies available space.
- Long-press on empty area opens the widget catalog.

## Interactions

| Action | Context | Result |
|--------|---------|--------|
| Long-press widget/empty area | Normal mode | Enter edit mode; long-pressed widget is selected |
| Tap widget | Edit mode | Select it (shows corner outline + X button) |
| Drag inside selected widget | Edit mode | Reposition — ghost preview at valid snap positions |
| Drop on empty cell | Edit mode | Place widget there |
| Drop on same-size widget | Edit mode | Swap positions |
| Drop on different-size occupied cell | Edit mode | Rejected (snaps back to original position) |
| Drag corner handle | Edit mode, resizable widget | Resize within min/max span |
| Tap (X) on selected widget | Edit mode | Remove widget from grid |
| Long-press empty area | Edit mode | Open widget catalog overlay |
| Double-tap widget | Normal mode | "More" action (fan/temp/LED control overlays) |
| Tap navbar checkmark | Edit mode | Save config & exit edit mode |

## Widget Catalog

- **Trigger**: Long-press empty grid area while in edit mode.
- **UI**: Overlay slides in from the right (uses existing `ui_nav_push_overlay()` pattern). Half-width or narrower so the grid remains partially visible.
- **Content**: Scrollable list of available widgets with icon + name + size badge (e.g., "2x2").
  - Already-placed widgets shown dimmed with "Placed" label.
  - Hardware-gated widgets hidden if hardware not detected.
- **Placement flow**: Tap a widget in the catalog -> catalog slides out -> widget auto-places near the grid cell where the long-press originated. Falls back to first available position if the preferred spot is occupied.

## Reflow Rules (v1)

Intentionally simple for v1 — no cascading push/reflow:

- **Same-size swap**: Drag a 1x1 onto another 1x1 = they swap positions.
- **Empty-only placement**: Drag onto empty cells that fit the widget's span.
- **Rejection**: Drag onto an occupied cell of a different size = widget snaps back to original position.
- **No cascade**: Moving one widget never displaces other widgets.

Future versions may add intelligent reflow with cascading push.

## Long-Press Conflict Resolution

Widgets that currently use long-press for "more" functionality must migrate to **double-tap**:

- **Fan stack** (`fan_stack`): long-press -> fan control overlay. Changes to double-tap.
- **Temp stack** (`temp_stack`): long-press -> temperature control overlay. Changes to double-tap.
- **LED widget** (`led`): long-press -> LED color picker overlay. Changes to double-tap.

Long-press is now globally reserved for edit mode entry on the home panel.

## Default Grid Layout (6×4, MEDIUM breakpoint)

```
  Col 0    Col 1    Col 2    Col 3    Col 4    Col 5
+--------+--------+--------+--------+--------+--------+
| Printer Image   | Tips (4×1)                         |  Row 0
|  (2×2)          |                                    |
+                 +--------+--------+--------+--------+
|                 |  (1×1 widgets, dynamic)             |  Row 1
+--------+--------+--------+--------+--------+--------+
| Print Status    |  (1×1 widgets, dynamic)             |  Row 2
|  (2×2)          |                                    |
+                 +--------+--------+--------+--------+
|                 |  (1×1 widgets, dynamic)             |  Row 3
+--------+--------+--------+--------+--------+--------+
```

**Anchor widgets** (fixed positions): printer_image (0,0 2×2), print_status (0,2 2×2), tips (2,0 4×1).

**1×1 widgets** are dynamically placed at populate time using **bottom-right-first packing**: free cells are scanned from (5,3) to (2,1), and widgets fill from the bottom-right corner upward. This ensures widgets cluster in the lower-right area and shift gracefully as hardware gates fire (adding/removing hardware-gated widgets).

## Config Persistence

- Uses existing `PanelWidgetConfig` with grid coordinates (`col`, `row`, `colspan`, `rowspan`).
- **Anchor widgets** always have explicit positions in config.
- **1×1 widgets** start with no positions (`col=-1, row=-1`) and get positions computed dynamically at each `populate_widgets()` call.
- Computed positions are written back to config entries **in-memory** after each populate.
- Positions are persisted to disk when **edit mode is entered** (stabilization snapshot) and when **edit mode is exited** (save user changes).
- Hardware gate observers trigger `populate_widgets()` on hardware discovery, causing dynamic re-packing as widgets appear/disappear.
- There is no single "hardware discovery complete" signal — AMS, power devices, sensors, and LEDs all discover asynchronously via different paths.

## Key Files

| File | Role |
|------|------|
| `src/ui/ui_panel_home.cpp` | Edit mode state machine, long-press handler, grid dots |
| `src/ui/ui_panel_home.h` | Edit mode state, selected widget tracking |
| `src/system/panel_widget_config.cpp` | Grid placement persistence |
| `src/ui/panel_widget_manager.cpp` | Widget population, grid descriptor management |
| `include/grid_layout.h` | Grid math, collision detection, available cell search |
| New: `ui_xml/widget_catalog_overlay.xml` | Widget catalog overlay layout |
| New: `src/ui/ui_widget_catalog_overlay.cpp` | Catalog overlay logic |
| `src/ui/panel_widgets/fan_stack_widget.cpp` | Double-tap migration |
| `src/ui/panel_widgets/temp_stack_widget.cpp` | Double-tap migration |
| `src/ui/panel_widgets/led_widget.cpp` | Double-tap migration |

## Constraints

- Landscape-first: most screens are in landscape mode. No bottom trays.
- Resource-constrained devices: minimize allocations during drag (reuse ghost/preview objects).
- Breakpoint-responsive: grid columns change at different screen sizes. Edit mode must handle the current breakpoint's grid dimensions.
- Max ~10 enabled widgets (existing UI constraint).
