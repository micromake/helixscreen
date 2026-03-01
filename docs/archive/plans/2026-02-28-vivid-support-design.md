# ViViD (BigTreeTech) AFC Support

**Date:** 2026-02-28
**Status:** Approved

## Summary

Add support for BigTreeTech ViViD multi-material units in the AFC filament management backend. ViViD uses a shared drive + selector stepper mechanism (like ERCF) but presents as HUB topology (like Box Turtle) from the UI perspective.

## Key Facts (from AFC firmware source)

| Property | Value |
|----------|-------|
| Klipper object prefix | `AFC_vivid` (lowercase) |
| Python class | `AFC_vivid` inherits `afcBoxTurtle` → `afcUnit` |
| `self.type` | `"ViViD"` |
| Units list entry format | `"ViViD vivid_1"` |
| Lane config section | `[AFC_lane <name>]` (same as OpenAMS) |
| Hub | Virtual (`switch_pin: "virtual"`) |
| Status format | Same as `afcUnit`: `lanes`, `extruders`, `hubs`, `buffers` |
| Topology | HUB (lanes merge through hub to single output) |

## Changes Required

### 1. Hardware Discovery (`include/printer_discovery.h`)
- Add `AFC_vivid` prefix to `parse_objects()` unit object detection
- ViViD lanes already handled (`AFC_lane` prefix exists)

### 2. AFC Backend (`src/printer/ams_backend_afc.cpp`)
- Parse `"ViViD vivid_1"` from units list → type=`"ViViD"`, name=`"vivid_1"`
- Map to Klipper key `"AFC_vivid vivid_1"` (existing transform handles this)
- Subscribe to `AFC_vivid` status updates in `handle_status_update()`
- Ensure HUB topology for ViViD units

### 3. Mock Backend (`include/ams_backend_mock.h`, `src/printer/ams_backend_mock.cpp`)
- Add ViViD mock mode for testing (4-lane unit, HUB topology)

### 4. No UI Changes
AMS panel already handles dynamic unit/lane counts and HUB topology.

## Scope

~50-80 lines across 3-4 files. Surgical additions following existing Box Turtle/OpenAMS patterns.
