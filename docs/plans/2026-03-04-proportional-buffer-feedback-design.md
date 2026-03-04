# Proportional Buffer Feedback & Filament Health Widget

**Date:** 2026-03-04
**Status:** Approved
**Related:** KlipperScreen-Happy-Hare-Edition PR #36 (proportional sensor display)

## Overview

Add proportional sync feedback visualization for Happy Hare's buffer sensor, enhance AFC's buffer modal with distance-to-fault context, and unify the clog detection widget with a buffer meter in a carousel-based "Filament Health" dashboard widget.

## Data Layer

### Happy Hare — New Fields

Parse from `mmu` Klipper object in `ams_backend_happy_hare.cpp`:

| Moonraker field | C++ field (AmsSystemInfo) | Type | Range | Description |
|-----------------|--------------------------|------|-------|-------------|
| `sync_feedback_bias_modelled` | `sync_feedback_bias` | float | [-1.0, 1.0] | EKF-filtered bias estimate. Negative = tension, positive = compression. Best for UI. |
| `sync_feedback_bias_raw` | `sync_feedback_bias_raw` | float | [-1.0, 1.0] | Direct sensor reading. For diagnostics. |

- `sync_feedback_bias_modelled` comes from HH's `MmuSyncFeedbackManager.get_status()`, merged into the top-level `mmu` object.
- When sync feedback is inactive or the sensor is a discrete switch type, the value snaps to {-1, 0, 1}.
- Proportional sensors (type "P") provide smooth continuous values; dual/single switch sensors (types "D", "CO", "TO") provide discrete values only.

### AFC — No Proportional Data

AFC TurtleNeck buffers have only two discrete switches (advance/trailing). No analog sensor exists.

- `distance_to_fault` is a **clog detection countdown** — mm of extrusion remaining before AFC declares a fault if the buffer switch hasn't retriggered. It is NOT a buffer position measurement.
- `rotation_distance` is the current extruder stepper multiplier (proportional output, not input).
- No changes to AFC data parsing needed for the buffer meter. We will enhance the modal display only.

### No Change to Canvas Buffer State

The filament path canvas continues to use the discrete `buffer_state` int (0=neutral, 1=compressed, 2=tension) for the simplified overview box. The proportional float is consumed only by the expanded buffer meter view.

## Visualization: UiBufferMeter (Happy Hare Only)

New drawing component, analogous to `UiClogMeter`.

### Visual Design

Two nested rectangles oriented vertically (perpendicular to horizontal filament path), representing the physical sliding plunger of the buffer:

- **Neutral (bias ≈ 0):** Inner rectangle overlaps outer rectangle by ~50%. A horizontal reference line marks the neutral position.
- **Tension (bias < 0):** Inner rectangle slides upward — minimal overlap, only bottom of outer and top of inner touch. Represents buffer being pulled/extended.
- **Compression (bias > 0):** Inner rectangle slides downward — near-complete overlap, outer on top. Represents buffer being pushed/compressed.
- **Fully extended (bias = -1.0):** Rectangles barely overlap.
- **Fully compressed (bias = +1.0):** Rectangles perfectly overlap.

### Annotations

- Neutral reference line with "Neutral" label
- Direction indicator: T (tension) / C (compression) / N (neutral)
- Percentage: `abs(bias * 100)`, shown alongside the rectangles
- Color: Green at neutral, transitioning through orange to red as `abs(bias)` approaches 1.0

### Sizing

Adapts to container — works in both small carousel widget pages and larger modal contexts.

## Integration Points

### 1. Canvas Overview (Both Backends)

**Happy Hare:**
- Buffer box color maps proportionally from `sync_feedback_bias` — green at neutral, orange/red as bias increases. Replaces current 3-state discrete color logic.
- Box shape/size unchanged (simplified view like hub/selector).

**AFC:**
- No change. Continues using discrete Advancing/Trailing state for color.

### 2. Buffer Click Modal

**Happy Hare — Upgraded Modal:**
- Replace `modal_show_alert` with a proper modal component.
- Top: `UiBufferMeter` visualization showing the sliding rectangles.
- Below: Text fields:
  - Sync Feedback: direction + percentage (e.g., "Tension 32%")
  - Bias (modelled): raw float value
  - eSpooler state (existing)
  - Sync Drive active/inactive (existing)
  - Clog Detection mode (existing)
  - Flow Rate percentage (existing)

**AFC — Enhanced Alert Modal:**
- Add distance-to-fault with explanation text: "Extrusion remaining before clog fault triggers"
- Show as: `Distance to Fault: 12.3 mm (45% of threshold)`
- Requires parsing `fault_sensitivity` or computing percentage from `distance_to_fault / (distance_to_fault + distance_consumed)` — TBD during implementation based on available data.
- Existing fields remain: State, Fault Detection enabled/disabled.

### 3. Dashboard Widget — Filament Health Carousel

Extend the existing `ClogDetectionWidget` to support carousel pages:

**Renamed:** "Clog Detection" → "Filament Health" (display name only; internal IDs unchanged for config compatibility).

**Page 1 — Clog Arc Meter:**
- Existing `UiClogMeter` arc visualization.
- Shown when encoder, flowguard, or AFC clog data is available.
- Existing config modal for source selection and thresholds.

**Page 2 — Buffer Meter:**
- `UiBufferMeter` sliding rectangle visualization.
- Shown when Happy Hare sync feedback is available and `sync_feedback_bias` is not disabled.
- Not shown for AFC (no proportional data).

**Conditional pages:** Only add pages for which data exists. If only one page has data, no carousel dots/swiping needed — just show that single page.

**Carousel integration** follows the established pattern (TempStackWidget, FanStackWidget):
- `ui_carousel_add_item()` for each page
- Observer-driven updates via `observe_int_sync` / `observe_string`
- Freeze+drain+clean on rebuild

### 4. Audit Existing Logic

During implementation, verify:
- `ams_backend_happy_hare.cpp` sync feedback parsing matches current HH `get_status()` structure (the `sync_feedback` nested dict for flow_rate, plus top-level `sync_feedback_state`).
- `ui_ams_detail.cpp` buffer state mapping (compressed/tension → 1/2) is correct.
- Clog detection widget source selection handles all backend types correctly.
- Buffer click callback registration and hit detection in `ui_filament_path_canvas.cpp` works for both backends.

## Files Affected

### New Files
- `src/ui/ui_buffer_meter.cpp` / `include/ui_buffer_meter.h` — UiBufferMeter drawing component
- `ui_xml/components/buffer_detail_modal.xml` — Happy Hare buffer modal layout

### Modified Files
- `include/ams_types.h` — Add `sync_feedback_bias`, `sync_feedback_bias_raw` to AmsSystemInfo
- `src/printer/ams_backend_happy_hare.cpp` — Parse new fields from MMU status
- `src/ui/ui_panel_ams.cpp` — Upgrade `handle_buffer_click()` for HH modal; enhance AFC modal text
- `src/ui/ui_filament_path_canvas.cpp` — Proportional color mapping for HH buffer box on canvas
- `src/ui/panel_widgets/clog_detection_widget.cpp` / `.h` — Carousel support, buffer meter page
- `ui_xml/components/panel_widget_clog_detection.xml` — Add carousel container

## Out of Scope

- Proportional buffer visualization for AFC (no hardware sensor exists)
- Feeding `sync_feedback_bias_modelled` into the clog arc meter as a data source (separate concern)
- Auto-scrolling or animation of the buffer meter rectangles (static position based on current value)
- Changes to the clog detection config modal
