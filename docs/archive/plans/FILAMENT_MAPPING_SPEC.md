# Filament Mapping at Print Start — Feature Specification

**Status**: Draft
**Date**: 2025-03-10
**Related**: `docs/devel/FILAMENT_MANAGEMENT.md`, `ui_xml/print_file_detail.xml`

---

## Overview

When a user starts a print on a printer with an AMS, MMU, or tool changer, HelixScreen
should let them choose which filament slot/tool feeds each G-code tool reference. This
replaces the current passive color-mismatch warning with an interactive mapping card on
the print details page, using firmware-level tool remapping commands.

## Goals

1. Let users assign AMS slots or tool changer tools to G-code tool references before printing
2. Auto-match by color/material as intelligent defaults, respecting existing firmware mappings
3. Warn on material type mismatches (e.g., G-code expects PETG but slot has PLA)
4. Work across all supported filament systems (AFC, Happy Hare, ValgACE, Tool Changer)
5. Restore the original tool mapping after the print completes, cancels, or errors
6. Support both single-tool and multi-tool prints

## Non-Goals

- G-code modification or injection (all remapping is firmware-level)
- Persisting per-file mapping preferences across prints
- Automatic filament loading/unloading to satisfy the mapping

---

## Architecture

### Remap Mechanism Per Backend

All remapping uses existing firmware commands. HelixScreen never modifies G-code.

| Backend | Remap Command | Reset Command | Status |
|---------|--------------|---------------|--------|
| **AFC** | `SET_MAP LANE=<name> MAP=T<n>` | `SET_MAP LANE=<name> MAP=T<original>` | Backend ready (`set_tool_mapping()`) |
| **Happy Hare** | `MMU_TTG_MAP TOOL=<n> GATE=<slot>` | `MMU_TTG_MAP TOOL=<n> GATE=<original>` | Backend ready (`set_tool_mapping()`) |
| **Tool Changer** | `ASSIGN_TOOL T<n> S<m>` | `ASSIGN_TOOL T<n> S<n>` (identity) | **New**: add to backend |
| **ValgACE** | N/A | N/A | Read-only display (single path, no remap) |

### Print-Time Remap Lifecycle

```
User selects file → Print details open
    │
    ├─ AMS/toolchanger detected?
    │   NO → No mapping card shown (existing behavior)
    │   YES ↓
    │
    ├─ Build default mapping (see "Default Selection Logic" below)
    ├─ Show "Filament" card with per-tool rows
    ├─ User adjusts mapping (optional)
    │
    ├─ User taps "Print"
    │   ├─ Save current firmware mapping as restore point
    │   ├─ Send remap commands for any changed tools
    │   ├─ Start print via PrintPreparationManager
    │   │
    │   ├─ Print completes / cancels / errors
    │   │   └─ Restore original firmware mapping
    │   │
    │   └─ HelixScreen crashes mid-print (edge case)
    │       └─ Firmware keeps "print" mapping; print finishes correctly
    │          On next launch, detect stale override via mapping comparison
```

### Default Selection Logic

For each tool `T<n>` referenced in the G-code:

1. **Check current firmware mapping** via `SlotRegistry::slot_for_tool(n)`
   - If a slot is mapped and has filament loaded → use as default
2. **Color match** against available AMS slots
   - Find closest color within `COLOR_MATCH_TOLERANCE` (40 RGB distance)
   - Prefer slots with matching material type
3. **"Auto"** — no explicit mapping, let firmware decide

This respects any mapping the user configured independently on the AMS panel or in
firmware config, while providing intelligent fallbacks for new prints.

### Restore Behavior

- Mapping is restored to pre-print state when the print ends (complete, cancel, or error)
- The "next print" always starts fresh — no stale mappings carry over
- If the user changes the mapping on the AMS panel during a print, that becomes the
  new "current" state; the restore target is the state captured at print start

---

## Data Fixes Required

### 1. Per-Tool Filament Type Parsing

**Problem**: Moonraker provides semicolon-separated types (e.g., `"PLA;PLA;PETG"`) but
`process_metadata_result()` in `ui_panel_print_select.cpp` only extracts the first value.

**Fix**: Parse into `std::vector<std::string>` for per-tool material type.

**Files**:
- `include/print_file_data.h` — add `std::vector<std::string> filament_types`
- `src/ui/ui_panel_print_select.cpp` — split on `;` in `process_metadata_result()`
- `include/moonraker_types.h` — `filament_type` already has the raw string

### 2. Filament Colors Transfer Gap

**Problem**: `FileMetadata.filament_colors` is parsed from Moonraker but never transferred
to `PrintFileData.filament_colors` in the `MetadataUpdate` struct.

**Fix**: Add `filament_colors` to `MetadataUpdate` and copy in `process_metadata_result()`.

**Files**:
- `src/ui/ui_panel_print_select.cpp` — add to `MetadataUpdate` struct and transfer logic

### 3. Tool Changer Remap Support

**Problem**: `ams_backend_toolchanger.cpp` reports `supports_tool_mapping = false` and
`get_tool_mapping_capabilities()` returns `{false, false, ""}`.

**Fix**: Implement `set_tool_mapping()` using Klipper's `ASSIGN_TOOL T<n> S<m>` command.
Update capabilities to `{true, true, "Tool reassignment via ASSIGN_TOOL"}`.

**Files**:
- `src/printer/ams_backend_toolchanger.cpp` — implement `set_tool_mapping()`
- `include/ams_backend_toolchanger.h` — update capabilities

**Note**: The existing `SELECT_TOOL` usage for direct UI taps (AMS panel) remains
unchanged. `ASSIGN_TOOL` only affects how G-code `T<n>` commands resolve during a print.

---

## UI Design

### Placement

The **"Filament" card** replaces the existing `color_requirements_card` in the print
details right column (`ui_xml/print_file_detail.xml`). It appears between
`preprint_steps_section` and `options_card`.

```
┌─ Print Details ────────────────────────────────────────────┐
│                         │                                  │
│                         │  [history_status_row]            │
│    Thumbnail / 3D       │  [preprint_steps_section]        │
│       Preview           │                                  │
│                         │  ┌─ Filament ─────────────────┐  │
│                         │  │  T0 ● ── [Slot 2: Red PLA]│  │
│  ┌──────────────────┐   │  │  T1 ● ── [Slot 4: Blu PET]│  │
│  │ filename.gcode   │   │  │  T2 ● ── [⚠ Select...    ]│  │
│  │ 2h 15m | 48g     │   │  │                            │  │
│  │ 0.2mm | 200 lyrs │   │  │  ⚠ T2: PETG expected,     │  │
│  └──────────────────┘   │  │    Slot 3 has PLA           │  │
│                         │  └────────────────────────────┘  │
│                         │                                  │
│                         │  ┌─ Pre-Print Options ────────┐  │
│                         │  │  Auto Bed Mesh      [====] │  │
│                         │  │  Quad Gantry Level  [====] │  │
│                         │  └────────────────────────────┘  │
│                         │                                  │
│                         │  [  Delete  ]    [   Print   ]   │
└─────────────────────────┴──────────────────────────────────┘
```

### Visibility Rules

| Condition | Behavior |
|-----------|----------|
| No AMS/toolchanger detected | Card hidden (existing behavior) |
| AMS detected, single-tool print | Card shown, single row (no "T0" prefix) |
| AMS detected, multi-tool print | Card shown, one row per tool |
| ValgACE (no remap) | Card shown read-only, informational only |
| Tool changer | Card shown, remappable rows |

### Mapping Row Layout

Each row in the card:

```
┌──────────────────────────────────────────────────────┐
│  T0  ●                     [● Slot 2: Red PLA     ▸] │
│      (swatch from gcode)    (tappable, opens modal)   │
└──────────────────────────────────────────────────────┘
```

- **Left**: Tool number + color swatch from G-code metadata
- **Right**: Tappable selector showing currently mapped slot with its color swatch,
  material name, and chevron indicator. Opens a modal picker on tap.
- For single-tool prints, omit the "T0" label.

### Slot Picker Modal

Tapping a mapping row opens a modal listing available slots:

```
┌─ Select Filament for T0 ────────────────────┐
│                                              │
│  ○  Auto (best match)                        │
│  ─────────────────────────────────────────── │
│  ●  Slot 1: Red PLA         250g     ✓      │
│  ●  Slot 2: Blue PETG       180g            │
│  ●  Slot 3: White PLA       320g            │
│  ○  Slot 4: (empty)                  grayed │
│  ●  Slot 5: Red PLA         90g      ⚠ low │
│                                              │
│            [ Cancel ]   [  Select  ]         │
└──────────────────────────────────────────────┘
```

- **"Auto"** option at top — lets firmware use its current mapping
- Each slot shows: color swatch, slot number, material, remaining weight
- Empty slots grayed out but visible
- Low weight warning for spools under a threshold (e.g., < 50g)
- Currently selected slot has a checkmark
- Material type mismatch highlighted (e.g., slot has PLA, G-code expects PETG)

### Warning Banner

If any tool has a material type mismatch, a warning banner appears at the bottom of
the card:

```
┌─ ⚠ ──────────────────────────────────────────┐
│  T2 expects PETG but Slot 3 has PLA           │
└───────────────────────────────────────────────┘
```

- Uses `ui_severity_card` with `severity="warning"`
- Multiple mismatches listed, one per line
- Does NOT block printing — user can override

### Print Button Behavior

- Print button is **always enabled** (no blocking on unmapped tools)
- Unmapped tools use "Auto" (firmware decides)
- Material warnings are advisory only
- Existing filament runout and printer state checks remain unchanged

---

## Implementation

### New / Modified Files

| File | Change |
|------|--------|
| `ui_xml/print_file_detail.xml` | Replace `color_requirements_card` with filament mapping card |
| `ui_xml/components/filament_mapping_row.xml` | **New**: reusable row component |
| `ui_xml/filament_picker_modal.xml` | **New**: slot picker modal layout |
| `src/ui/ui_filament_mapping_card.cpp/.h` | **New**: card controller (builds rows, manages state) |
| `src/ui/ui_filament_picker_modal.cpp/.h` | **New**: modal controller |
| `src/ui/ui_print_select_detail_view.cpp/.h` | Integrate mapping card, remove old color swatch logic |
| `src/ui/ui_print_start_controller.cpp/.h` | Save/send/restore mapping, replace color mismatch check |
| `include/print_file_data.h` | Add `filament_types` vector |
| `src/ui/ui_panel_print_select.cpp` | Fix metadata transfer (colors + per-tool types) |
| `src/printer/ams_backend_toolchanger.cpp/.h` | Add `ASSIGN_TOOL` remap support |
| `src/printer/ams_state.cpp/.h` | Add mapping snapshot/restore helpers |
| `tests/unit/test_filament_mapping.cpp` | **New**: unit tests for mapping logic |
| `tests/unit/test_ams_tool_mapping.cpp` | Add toolchanger `ASSIGN_TOOL` tests |

### Key Classes

```cpp
/// Per-tool mapping entry
struct ToolMapping {
    int tool_index;                  ///< G-code tool number (0-based)
    uint32_t gcode_color;            ///< Expected color from slicer
    std::string gcode_material;      ///< Expected material type ("PLA", "PETG", etc.)
    int mapped_slot;                 ///< AMS slot index (-1 = auto)
    int mapped_backend;              ///< Backend index (-1 = primary)
    bool material_mismatch;          ///< True if slot material != gcode material
};

/// Manages the filament mapping card on the print details page
class FilamentMappingCard {
public:
    void create(lv_obj_t* parent);
    void update(const std::vector<std::string>& gcode_colors,
                const std::vector<std::string>& gcode_materials);
    void hide();

    /// Get the current user-configured mapping
    std::vector<ToolMapping> get_mappings() const;

private:
    void build_default_mappings();
    void update_warning_banner();
    void on_row_tapped(int tool_index);

    lv_obj_t* card_ = nullptr;
    lv_obj_t* rows_container_ = nullptr;
    lv_obj_t* warning_banner_ = nullptr;
    std::vector<ToolMapping> mappings_;
};

/// Manages the slot picker modal
class FilamentPickerModal : public Modal {
public:
    using SelectCallback = std::function<void(int slot_index, int backend_index)>;

    static void show(int tool_index, uint32_t expected_color,
                     const std::string& expected_material,
                     SelectCallback on_select);

    std::string get_name() const override { return "Filament Picker"; }
    std::string component_name() const override { return "filament_picker_modal"; }

private:
    void populate_slot_list();
    void on_ok() override;
};
```

### Remap Flow in PrintStartController

```cpp
void PrintStartController::initiate() {
    // ... existing printer state + filament sensor checks ...

    // Get user's mapping from the card
    auto mappings = filament_mapping_card_.get_mappings();

    // Snapshot current firmware mapping for restore
    saved_mapping_ = snapshot_current_mapping();

    // Apply remaps for any non-auto mappings that differ from current
    for (const auto& m : mappings) {
        if (m.mapped_slot >= 0 && m.mapped_slot != saved_mapping_[m.tool_index]) {
            backend->set_tool_mapping(m.tool_index, m.mapped_slot);
        }
    }

    // Proceed with print start
    execute_print_start();
}

void PrintStartController::on_print_ended() {
    // Restore original mapping
    for (size_t i = 0; i < saved_mapping_.size(); ++i) {
        if (saved_mapping_[i] != current_mapping(i)) {
            backend->set_tool_mapping(i, saved_mapping_[i]);
        }
    }
    saved_mapping_.clear();
}
```

**Thread safety**: Remap commands go through `execute_gcode()` which posts to the
WebSocket thread. The restore callback fires from the print status observer. Both use
`ui_queue_update()` to stay on the LVGL thread. Per [L072], async callbacks must use
`weak_ptr<bool>` alive guards, not bare `this` capture.

---

## Implementation Stages

| Stage | Scope | Deliverables |
|-------|-------|-------------|
| **1. Data fixes** | Fix metadata transfer gaps | Per-tool filament types, filament_colors transfer |
| **2. Mapping logic** | Core mapping engine (no UI) | `ToolMapping` struct, default selection algorithm, unit tests |
| **3. Mapping card** | UI card on print details | XML layout, `FilamentMappingCard` class, replaces color swatches |
| **4. Picker modal** | Slot selection modal | XML layout, `FilamentPickerModal` class |
| **5. Remap integration** | Wire to PrintStartController | Save/send/restore cycle, material warnings |
| **6. Toolchanger support** | Add `ASSIGN_TOOL` to backend | `ams_backend_toolchanger.cpp` changes, tests |
| **7. Polish** | Edge cases, testing | ValgACE read-only mode, crash recovery, multi-backend |

---

## Edge Cases

| Scenario | Behavior |
|----------|----------|
| G-code has no color metadata | Show mapping card with gray swatches, auto-match by slot order |
| More tools than AMS slots | Unmapped tools default to "Auto" |
| AMS slot becomes empty mid-selection | Reactive update via AmsState subjects |
| User navigates away and back | Mapping state preserved in `FilamentMappingCard` |
| Print starts while remapping in progress | Remap commands are synchronous G-code; print waits |
| ValgACE (no remap support) | Read-only card: shows loaded filament info, no picker |
| Multiple AMS backends | Picker modal shows slots from all backends, grouped by unit |
| User changed mapping on AMS panel before opening print details | Default selection respects firmware's current mapping |

---

## Related Files

**Existing (to read/understand)**:
- `src/ui/ui_print_start_controller.cpp` — current pre-print check flow
- `src/ui/ui_print_select_detail_view.cpp` — detail view management
- `src/ui/ui_ams_edit_modal.cpp` — modal pattern reference
- `src/printer/ams_state.cpp` — reactive state management
- `include/slot_registry.h` — tool-to-slot mapping data
- `include/ams_types.h` — `SlotInfo`, `ToolMappingCapabilities`
- `tests/unit/test_ams_tool_mapping.cpp` — existing mapping tests

**Existing (to modify)**:
- `ui_xml/print_file_detail.xml` — replace color_requirements_card
- `src/ui/ui_print_start_controller.cpp/.h` — remap lifecycle
- `src/ui/ui_print_select_detail_view.cpp/.h` — integrate mapping card
- `src/ui/ui_panel_print_select.cpp` — metadata transfer fixes
- `include/print_file_data.h` — add filament_types vector
- `src/printer/ams_backend_toolchanger.cpp/.h` — ASSIGN_TOOL support

**New**:
- `ui_xml/components/filament_mapping_row.xml`
- `ui_xml/filament_picker_modal.xml`
- `src/ui/ui_filament_mapping_card.cpp/.h`
- `src/ui/ui_filament_picker_modal.cpp/.h`
- `include/ui_filament_mapping_card.h`
- `include/ui_filament_picker_modal.h`
- `tests/unit/test_filament_mapping.cpp`
