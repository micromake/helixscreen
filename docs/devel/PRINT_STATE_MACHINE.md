# PrintLifecycleState -- Print State Machine

## Overview

`PrintLifecycleState` is a pure-logic state machine (no LVGL dependencies) that maps
Moonraker's raw `PrintJobState` + `PrintOutcome` into UI-level `PrintState` values.
It guards against Moonraker race conditions where zeroed progress/layer/duration
values arrive after a print has already reached a terminal state (Complete, Cancelled,
Error). The UI layer consumes `StateChangeResult` structs to react to transitions
without embedding widget logic in the state machine.

**Files:**
- `include/print_lifecycle_state.h` -- enum, result struct, class declaration
- `src/printer/print_lifecycle_state.cpp` -- transition logic and guards

---

## State Transition Diagram

```
                          start_phase != 0
                   +---------------------------+
                   |                           |
                   v                           |
               Preparing --+                   |
               (UI-only)   |                   |
                   |       | start_phase == 0  |
                   |       | (restore from     |
                   |       |  job_state)        |
                   |       v                   |
     STANDBY   PRINTING   PAUSED              |
    +-------> Idle    Printing <---> Paused    |
    |           ^      |    ^         |        |
    |           |      |    +---------+        |
    |           |      |   PAUSED/PRINTING     |
    |           |      |                       |
    |  STANDBY  |      +-------+-------+-------+
    |  (from    |      |       |       |
    |  any)     |  COMPLETE CANCELLED ERROR
    |           |      |       |       |
    |           |      v       v       v
    |           |   Complete Cancelled Error
    |           |      |       |       |
    |           +------+-------+-------+
    |              STANDBY (print_ended)
    +------------------------------------------+
```

Key points:
- **Preparing** is a UI-only state driven by `on_start_phase_changed()`, not by
  Moonraker's `job_state`. It represents pre-print operations (homing, bed leveling,
  heating). When `start_phase` returns to 0, state restores from the current
  `job_state` (typically Printing).
- Terminal states (Complete, Cancelled, Error) persist until Moonraker sends STANDBY,
  which transitions to Idle and fires `print_ended`.

---

## State Transition Table

### Transitions from `on_job_state_changed()`

| From | To | Moonraker Trigger | Side-Effects |
|------|----|-------------------|--------------|
| Any | Idle | STANDBY | `print_ended=true`, `clear_gcode_loaded=true`, `should_show_viewer=false` |
| Idle / Preparing | Printing | PRINTING | `should_reset_progress_bar=true`, `should_clear_excluded_objects=true`, `should_show_viewer` (if gcode loaded) |
| Paused | Printing | PRINTING | (resume -- no reset, no clear) |
| Printing | Paused | PAUSED | `should_show_viewer` preserved |
| Any active | Complete | COMPLETE | `should_freeze_complete=true`: progress forced to 100, layer forced to total, remaining forced to 0, elapsed frozen. `should_show_viewer` preserved |
| Any active | Cancelled | CANCELLED | `should_animate_cancelled=true`, `should_show_viewer` preserved |
| Any active | Error | ERROR | `should_animate_error=true`, `should_show_viewer` preserved |

### Transitions from `on_start_phase_changed()`

| From | To | Trigger | Notes |
|------|----|---------|-------|
| Any | Preparing | `phase != 0` | Resets `preprint_elapsed` and `preprint_remaining` to 0 |
| Preparing | Printing | `phase == 0`, `job_state == PRINTING` | Restores state from current Moonraker job_state |
| Preparing | Paused | `phase == 0`, `job_state == PAUSED` | (unlikely but handled) |
| Preparing | Idle | `phase == 0`, `job_state == other` | Fallback |

---

## Guards

The state machine rejects stale updates that Moonraker sends after a print ends.

### Terminal state guards (Complete, Cancelled, Error)

These methods return `false` and discard the update:
- `on_progress_changed()` -- prevents progress resetting to 0
- `on_layer_changed()` -- prevents layer count resetting to 0
- `on_duration_changed()` -- prevents elapsed time resetting
- `on_time_left_changed()` -- prevents remaining time resetting

### Outcome guards

`on_duration_changed()` and `on_time_left_changed()` also reject updates when
`outcome != PrintOutcome::NONE`. This catches the case where Moonraker reports
a completed/cancelled outcome before the state machine has transitioned.

### Preparing state guards

During Preparing, `on_duration_changed()` and `on_time_left_changed()` store the
value internally but return `false`. This signals the UI that the preprint observer
owns the time display, not the normal print timer. Preprint time is updated
separately via `on_preprint_elapsed_changed()` and `on_preprint_remaining_changed()`.

### Always accepted

These are never guarded and update in all states:
- `on_temperature_changed()` -- nozzle/bed current and target
- `on_speed_changed()` -- speed override percentage
- `on_flow_changed()` -- flow override percentage

---

## Resource Lifecycle

### print_ended

`print_ended` fires **only** on transition to Idle (when Moonraker sends STANDBY
after a terminal state). It does NOT fire on transition to Complete, Cancelled, or
Error. This allows the UI to keep resources (thumbnail, stats overlay, viewer) visible
while the user reviews the final print state. The UI tears down print resources only
when `print_ended` is true.

### gcode_loaded

`gcode_loaded` is preserved through terminal states so the 3D viewer stays visible
showing where the print stopped. It is cleared only on transition to Idle
(`clear_gcode_loaded=true`). The flag can be set externally via `set_gcode_loaded()`.

### 3D Viewer visibility

`should_show_viewer` in `StateChangeResult` is computed as `want_viewer && gcode_loaded`.
`want_viewer()` returns true for all states except Idle -- this means the viewer
remains visible through Preparing, Printing, Paused, Complete, Cancelled, and Error.
The viewer disappears only when transitioning to Idle.

### Complete state freeze

On transition to Complete, the state machine freezes display values:
- `progress` = 100
- `current_layer` = `total_layers` (if total > 0)
- `remaining_seconds` = 0
- `elapsed_seconds` = frozen at last known value

This prevents Moonraker's post-completion zeroed values from corrupting the display.
