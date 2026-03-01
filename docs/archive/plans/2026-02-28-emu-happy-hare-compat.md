# EMU / Type B Happy Hare Compatibility Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make HelixScreen handle EMU (Type B, VirtualSelector) Happy Hare configurations alongside existing ERCF/Tradrack formats without crashing or losing data.

**Architecture:** All changes are additive `else if` branches in the existing `parse_mmu_state()` function (`src/printer/ams_backend_happy_hare.cpp`). Each parsing block gets format detection so it handles both old (integer/per-gate) and new (float-array/aggregate) formats. TDD throughout — write failing test first, then fix parser.

**Tech Stack:** C++17, nlohmann::json, Catch2, LVGL types (uint32_t color)

---

### Task 1: Fix `num_gates` Integer Parsing

The EMU sends `num_gates` as a plain integer `8`. Our parser only handles string format (`"6,4"`). Without this, `per_unit_gate_counts_` stays empty and the unit structure may not build correctly.

**Files:**
- Test: `tests/unit/test_ams_backend_happy_hare.cpp`
- Modify: `src/printer/ams_backend_happy_hare.cpp:361-405`

**Step 1: Write the failing test**

In `test_ams_backend_happy_hare.cpp`, add a new test section near the existing multi-unit tests:

```cpp
TEST_CASE_METHOD(AmsBackendHappyHareTestHelper, "EMU num_gates as integer",
                 "[ams][happy_hare][emu]") {
    // EMU sends num_gates as plain integer, not string
    nlohmann::json mmu_data = {
        {"gate_status", {1, 1, 1, 1, 1, 1, 1, 1}},
        {"num_gates", 8}
    };
    test_parse_mmu_state(mmu_data);

    auto info = get_system_info();
    REQUIRE(info.total_slots == 8);
    REQUIRE(info.units.size() == 1);
    REQUIRE(info.units[0].slot_count == 8);
}

TEST_CASE_METHOD(AmsBackendHappyHareTestHelper, "EMU num_gates as array",
                 "[ams][happy_hare][emu]") {
    // Config format sends num_gates as [8] array
    nlohmann::json mmu_data = {
        {"gate_status", {1, 1, 1, 1, 1, 1, 1, 1}},
        {"num_gates", {8}}
    };
    test_parse_mmu_state(mmu_data);

    auto info = get_system_info();
    REQUIRE(info.total_slots == 8);
    REQUIRE(info.units.size() == 1);
    REQUIRE(info.units[0].slot_count == 8);
}
```

**Step 2: Run tests to verify they fail**

Run: `make test-run` (or `./build/bin/helix-tests "[emu]"`)
Expected: FAIL — num_gates integer/array not parsed, per_unit_gate_counts_ empty

**Step 3: Add integer and array handling to num_gates parser**

In `parse_mmu_state()`, after the existing `ng.is_string()` branch (~line 380), add:

```cpp
    } else if (ng.is_number_integer()) {
        // EMU sends plain integer (single unit)
        int count = ng.get<int>();
        if (count > 0) {
            per_unit_gate_counts_ = {count};
            spdlog::debug("[AMS HappyHare] Single-unit gate count from num_gates int: {}",
                          count);
        }
    } else if (ng.is_array()) {
        // Config format: [8] or [6, 4]
        std::vector<int> counts;
        for (const auto& c : ng) {
            if (c.is_number_integer()) {
                int count = c.get<int>();
                if (count > 0) counts.push_back(count);
            }
        }
        if (!counts.empty()) {
            per_unit_gate_counts_ = counts;
            spdlog::debug("[AMS HappyHare] Per-unit gate counts from num_gates array");
        }
    }
```

**Step 4: Run tests to verify they pass**

Run: `make test-run` (or `./build/bin/helix-tests "[emu]"`)
Expected: PASS

**Step 5: Commit**

```
git add tests/unit/test_ams_backend_happy_hare.cpp src/printer/ams_backend_happy_hare.cpp
git commit -m "fix(ams): handle num_gates as integer or array for EMU"
```

---

### Task 2: Fix `gate_color_rgb` Float Array Parsing

EMU sends `gate_color_rgb` as `[[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], ...]` (per-channel floats 0.0-1.0) instead of hex integers `[0xFF0000, 0x00FF00, ...]`. Also add `gate_color` hex string fallback.

**Files:**
- Test: `tests/unit/test_ams_backend_happy_hare.cpp`
- Modify: `src/printer/ams_backend_happy_hare.cpp:445-457`

**Step 1: Write the failing tests**

```cpp
TEST_CASE_METHOD(AmsBackendHappyHareTestHelper, "EMU gate_color_rgb as float arrays",
                 "[ams][happy_hare][emu]") {
    initialize_test_gates(4);
    nlohmann::json mmu_data = {
        {"gate_color_rgb", {
            {1.0, 0.0, 0.0},       // Red
            {0.0, 1.0, 0.0},       // Green
            {0.0, 0.0, 1.0},       // Blue
            {0.976, 0.976, 0.4}    // Yellowish
        }}
    };
    test_parse_mmu_state(mmu_data);

    auto info = get_system_info();
    REQUIRE(info.units[0].slots[0].color_rgb == 0xFF0000);
    REQUIRE(info.units[0].slots[1].color_rgb == 0x00FF00);
    REQUIRE(info.units[0].slots[2].color_rgb == 0x0000FF);
    // 0.976*255 = 248.88 → 0xF9, 0.4*255 = 102 → 0x66
    REQUIRE(info.units[0].slots[3].color_rgb == 0xF9F966);
}

TEST_CASE_METHOD(AmsBackendHappyHareTestHelper, "gate_color hex string fallback",
                 "[ams][happy_hare][emu]") {
    initialize_test_gates(3);
    // gate_color_rgb absent, fall back to gate_color hex strings
    nlohmann::json mmu_data = {
        {"gate_color", {"ffffff", "000000", "042f56"}}
    };
    test_parse_mmu_state(mmu_data);

    auto info = get_system_info();
    REQUIRE(info.units[0].slots[0].color_rgb == 0xFFFFFF);
    REQUIRE(info.units[0].slots[1].color_rgb == 0x000000);
    REQUIRE(info.units[0].slots[2].color_rgb == 0x042F56);
}
```

**Step 2: Run tests to verify they fail**

Run: `./build/bin/helix-tests "[emu]"`
Expected: FAIL — float arrays skipped by `is_number_integer()`, gate_color not parsed

**Step 3: Extend gate_color_rgb parsing, add gate_color fallback**

Replace the existing `gate_color_rgb` block (~lines 445-457) with:

```cpp
    // Parse gate_color_rgb: integer array [0xRRGGBB, ...] or float array [[R,G,B], ...]
    bool colors_parsed = false;
    if (mmu_data.contains("gate_color_rgb") && mmu_data["gate_color_rgb"].is_array()) {
        const auto& colors = mmu_data["gate_color_rgb"];
        for (size_t i = 0; i < colors.size(); ++i) {
            auto* entry = slots_.get_mut(static_cast<int>(i));
            if (!entry) continue;

            if (colors[i].is_number_integer()) {
                // Traditional format: 0xRRGGBB integer
                entry->info.color_rgb = static_cast<uint32_t>(colors[i].get<int>());
                colors_parsed = true;
            } else if (colors[i].is_array() && colors[i].size() >= 3) {
                // EMU format: [R, G, B] floats 0.0-1.0
                auto r = static_cast<uint8_t>(std::clamp(colors[i][0].get<double>(), 0.0, 1.0) * 255.0 + 0.5);
                auto g = static_cast<uint8_t>(std::clamp(colors[i][1].get<double>(), 0.0, 1.0) * 255.0 + 0.5);
                auto b = static_cast<uint8_t>(std::clamp(colors[i][2].get<double>(), 0.0, 1.0) * 255.0 + 0.5);
                entry->info.color_rgb = (static_cast<uint32_t>(r) << 16) |
                                        (static_cast<uint32_t>(g) << 8) |
                                        static_cast<uint32_t>(b);
                colors_parsed = true;
            }
        }
    }

    // Fallback: parse gate_color hex strings ["ffffff", "000000", ...]
    if (!colors_parsed && mmu_data.contains("gate_color") && mmu_data["gate_color"].is_array()) {
        const auto& colors = mmu_data["gate_color"];
        for (size_t i = 0; i < colors.size(); ++i) {
            if (colors[i].is_string()) {
                auto* entry = slots_.get_mut(static_cast<int>(i));
                if (entry) {
                    try {
                        entry->info.color_rgb = static_cast<uint32_t>(
                            std::stoul(colors[i].get<std::string>(), nullptr, 16));
                    } catch (...) {
                        // Invalid hex, leave default
                    }
                }
            }
        }
    }
```

**Step 4: Run tests to verify they pass**

Run: `./build/bin/helix-tests "[emu]"`
Expected: PASS

**Step 5: Commit**

```
git add tests/unit/test_ams_backend_happy_hare.cpp src/printer/ams_backend_happy_hare.cpp
git commit -m "fix(ams): support float-array and hex-string color formats for EMU"
```

---

### Task 3: Fix `sensors` Aggregate Format Parsing

EMU sends `{"mmu_pre_gate": true, "mmu_gear": true, ...}` (aggregate for current gate) instead of per-gate `{"mmu_pre_gate_0": true, "mmu_pre_gate_1": false, ...}`.

**Files:**
- Test: `tests/unit/test_ams_backend_happy_hare.cpp`
- Modify: `src/printer/ams_backend_happy_hare.cpp:603-647`
- Modify: `include/slot_registry.h` (add `has_gear_sensor` / `gear_triggered` to `SlotSensors`)

**Step 1: Write the failing test**

```cpp
TEST_CASE_METHOD(AmsBackendHappyHareTestHelper, "EMU aggregate sensor format",
                 "[ams][happy_hare][emu]") {
    initialize_test_gates(4);

    // First update: gate 0 active, pre_gate triggered, gear triggered
    nlohmann::json mmu_data = {
        {"gate", 0},
        {"sensors", {
            {"mmu_pre_gate", true},
            {"mmu_gear", true},
            {"extruder", true},
            {"toolhead", true}
        }}
    };
    test_parse_mmu_state(mmu_data);

    auto info = get_system_info();
    // All gates should report having pre-gate sensors (EMU has them on every gate)
    REQUIRE(info.units[0].has_slot_sensors == true);

    // The current gate (0) should have its pre-gate sensor triggered
    auto slot0 = get_slot_entry(0);
    REQUIRE(slot0 != nullptr);
    REQUIRE(slot0->sensors.has_pre_gate_sensor == true);
    REQUIRE(slot0->sensors.pre_gate_triggered == true);
}
```

**Step 2: Run tests to verify they fail**

Run: `./build/bin/helix-tests "[emu]"`
Expected: FAIL — aggregate sensor keys not matched by prefix scan

**Step 3: Add aggregate sensor detection after per-gate loop**

After the existing per-gate sensor loop (around line 640), add:

```cpp
    // If no per-gate sensors found, check for aggregate sensor format (EMU)
    // EMU reports "mmu_pre_gate" (bool) and "mmu_gear" (bool) for the active gate only
    if (!any_sensor && sensors.contains("mmu_pre_gate")) {
        bool pre_gate_val = sensors["mmu_pre_gate"].is_boolean() &&
                            sensors["mmu_pre_gate"].get<bool>();
        bool gear_val = sensors.contains("mmu_gear") &&
                        sensors["mmu_gear"].is_boolean() &&
                        sensors["mmu_gear"].get<bool>();

        // Mark all gates as having sensors (EMU has pre-gate on every gate)
        for (int i = 0; i < slots_.count(); ++i) {
            auto* entry = slots_.get_mut(i);
            if (entry) {
                entry->sensors.has_pre_gate_sensor = true;
            }
        }

        // Update the current gate's sensor readings
        if (system_info_.current_slot >= 0) {
            auto* entry = slots_.get_mut(system_info_.current_slot);
            if (entry) {
                entry->sensors.pre_gate_triggered = pre_gate_val;
            }
        }

        any_sensor = true;
        spdlog::trace("[AMS HappyHare] Aggregate sensors: pre_gate={}, gear={}",
                      pre_gate_val, gear_val);
    }
```

**Step 4: Run tests to verify they pass**

Run: `./build/bin/helix-tests "[emu]"`
Expected: PASS

**Step 5: Commit**

```
git add tests/unit/test_ams_backend_happy_hare.cpp src/printer/ams_backend_happy_hare.cpp
git commit -m "fix(ams): handle aggregate sensor format for EMU"
```

---

### Task 4: Fix `drying_state` Array Format Parsing

EMU sends `drying_state` as an array of strings `["", "", ...]` (per-gate, empty = inactive) instead of the object format `{active: bool, ...}`.

**Files:**
- Test: `tests/unit/test_ams_backend_happy_hare.cpp`
- Modify: `src/printer/ams_backend_happy_hare.cpp:649-676`

**Step 1: Write the failing test**

```cpp
TEST_CASE_METHOD(AmsBackendHappyHareTestHelper, "EMU drying_state as array",
                 "[ams][happy_hare][emu]") {
    initialize_test_gates(4);

    SECTION("all empty strings means no active dryer") {
        nlohmann::json mmu_data = {
            {"drying_state", {"", "", "", ""}}
        };
        test_parse_mmu_state(mmu_data);

        auto dryer = get_dryer_info();
        // Array format indicates dryer hardware exists but is not active
        REQUIRE(dryer.supported == true);
        REQUIRE(dryer.active == false);
    }

    SECTION("existing object format still works") {
        nlohmann::json mmu_data = {
            {"drying_state", {
                {"active", true},
                {"current_temp", 55.0},
                {"target_temp", 60.0},
                {"remaining_min", 30},
                {"duration_min", 240},
                {"fan_pct", 75}
            }}
        };
        test_parse_mmu_state(mmu_data);

        auto dryer = get_dryer_info();
        REQUIRE(dryer.supported == true);
        REQUIRE(dryer.active == true);
        REQUIRE(dryer.current_temp_c == Catch::Approx(55.0));
    }
}
```

**Step 2: Run tests to verify they fail**

Run: `./build/bin/helix-tests "[emu]"`
Expected: The "all empty strings" section FAILS — array format not handled

**Step 3: Add array format handling to drying_state parser**

Replace the existing drying_state block (~lines 649-676) with:

```cpp
    // Parse drying_state: object (KMS/traditional) or array of strings (EMU per-gate)
    if (mmu_data.contains("drying_state")) {
        const auto& drying = mmu_data["drying_state"];
        if (drying.is_object()) {
            // Traditional object format: {active, current_temp, target_temp, ...}
            dryer_info_.supported = true;
            if (drying.contains("active") && drying["active"].is_boolean()) {
                dryer_info_.active = drying["active"].get<bool>();
            }
            if (drying.contains("current_temp") && drying["current_temp"].is_number()) {
                dryer_info_.current_temp_c = drying["current_temp"].get<float>();
            }
            if (drying.contains("target_temp") && drying["target_temp"].is_number()) {
                dryer_info_.target_temp_c = drying["target_temp"].get<float>();
            }
            if (drying.contains("remaining_min") && drying["remaining_min"].is_number_integer()) {
                dryer_info_.remaining_min = drying["remaining_min"].get<int>();
            }
            if (drying.contains("duration_min") && drying["duration_min"].is_number_integer()) {
                dryer_info_.duration_min = drying["duration_min"].get<int>();
            }
            if (drying.contains("fan_pct") && drying["fan_pct"].is_number_integer()) {
                dryer_info_.fan_pct = drying["fan_pct"].get<int>();
            }
            spdlog::trace("[AMS HappyHare] Dryer state (object): active={}, temp={:.1f}°C",
                          dryer_info_.active, dryer_info_.current_temp_c);
        } else if (drying.is_array()) {
            // EMU per-gate array format: ["", "", ...] (empty = inactive)
            // Hardware exists if we get this field at all
            dryer_info_.supported = true;
            bool any_active = false;
            for (const auto& entry : drying) {
                if (entry.is_string() && !entry.get<std::string>().empty()) {
                    any_active = true;
                    break;
                }
            }
            dryer_info_.active = any_active;
            spdlog::trace("[AMS HappyHare] Dryer state (array): supported=true, active={}",
                          any_active);
        }
    }
```

**Step 4: Run tests to verify they pass**

Run: `./build/bin/helix-tests "[emu]"`
Expected: PASS

**Step 5: Commit**

```
git add tests/unit/test_ams_backend_happy_hare.cpp src/printer/ams_backend_happy_hare.cpp
git commit -m "fix(ams): handle drying_state as array for EMU per-gate dryer"
```

---

### Task 5: Parse `gate_filament_name` as Filament Name Source

EMU sends `gate_filament_name` (not `gate_name`) for per-gate filament names like `["Matte White", "Matte Black", ...]`. Our code only checks `gate_name`.

**Files:**
- Test: `tests/unit/test_ams_backend_happy_hare.cpp`
- Modify: `src/printer/ams_backend_happy_hare.cpp:571-584`

**Step 1: Write the failing test**

```cpp
TEST_CASE_METHOD(AmsBackendHappyHareTestHelper, "EMU gate_filament_name parsing",
                 "[ams][happy_hare][emu]") {
    initialize_test_gates(3);

    SECTION("gate_filament_name used when gate_name is null") {
        nlohmann::json mmu_data = {
            {"gate_name", nullptr},
            {"gate_filament_name", {"Matte White", "Matte Black", "Matte Yellow"}}
        };
        test_parse_mmu_state(mmu_data);

        auto info = get_system_info();
        REQUIRE(info.units[0].slots[0].color_name == "Matte White");
        REQUIRE(info.units[0].slots[1].color_name == "Matte Black");
        REQUIRE(info.units[0].slots[2].color_name == "Matte Yellow");
    }

    SECTION("gate_name takes priority over gate_filament_name") {
        nlohmann::json mmu_data = {
            {"gate_name", {"Priority Name", "Other", "Third"}},
            {"gate_filament_name", {"Fallback", "Fallback", "Fallback"}}
        };
        test_parse_mmu_state(mmu_data);

        auto info = get_system_info();
        REQUIRE(info.units[0].slots[0].color_name == "Priority Name");
    }
}
```

**Step 2: Run tests to verify they fail**

Run: `./build/bin/helix-tests "[emu]"`
Expected: FAIL — gate_filament_name not parsed

**Step 3: Add gate_filament_name parsing after gate_name block**

After the existing `gate_name` parsing block (~line 584), add:

```cpp
    // Fallback: parse gate_filament_name (EMU uses this instead of gate_name)
    if (mmu_data.contains("gate_filament_name") && mmu_data["gate_filament_name"].is_array()) {
        const auto& names = mmu_data["gate_filament_name"];
        for (size_t i = 0; i < names.size(); ++i) {
            if (names[i].is_string()) {
                auto* entry = slots_.get_mut(static_cast<int>(i));
                if (entry && entry->info.color_name.empty()) {
                    entry->info.color_name = names[i].get<std::string>();
                }
            }
        }
        spdlog::trace("[AMS HappyHare] Parsed gate_filament_name for {} gates", names.size());
    }
```

The `color_name.empty()` check ensures `gate_name` takes priority if both are present.

**Step 4: Run tests to verify they pass**

Run: `./build/bin/helix-tests "[emu]"`
Expected: PASS

**Step 5: Commit**

```
git add tests/unit/test_ams_backend_happy_hare.cpp src/printer/ams_backend_happy_hare.cpp
git commit -m "fix(ams): parse gate_filament_name for EMU filament names"
```

---

### Task 6: Full EMU Integration Test

Simulate a complete EMU status update using the real data from the user's Moonraker dump. This catches any interaction effects between the individual fixes.

**Files:**
- Test: `tests/unit/test_ams_backend_happy_hare.cpp`

**Step 1: Write the integration test**

```cpp
TEST_CASE_METHOD(AmsBackendHappyHareTestHelper, "Full EMU status integration",
                 "[ams][happy_hare][emu]") {
    // Real data from EMU user's Moonraker dump (simplified)
    nlohmann::json mmu_data = {
        {"gate", 0},
        {"tool", 0},
        {"filament", "Loaded"},
        {"action", "Idle"},
        {"num_gates", 8},
        {"filament_pos", 10},
        {"has_bypass", true},
        {"gate_status", {2, 2, 2, 2, 2, 2, 1, 1}},
        {"gate_color_rgb", {
            {1.0, 1.0, 1.0},
            {0.0, 0.0, 0.0},
            {0.976, 0.976, 0.4},
            {0.016, 0.184, 0.337},
            {0.553, 0.784, 0.588},
            {0.0, 0.0, 0.0},
            {1.0, 1.0, 1.0},
            {0.0, 0.0, 0.0}
        }},
        {"gate_material", {"PLA", "PLA", "PLA", "PLA", "PLA", "ABS", "ABS", "ASA CF"}},
        {"gate_filament_name", {"Matte White", "Matte Black", "Matte Yellow", "Matte Navy",
                                "Matte Green", "Black", "White", "Black"}},
        {"gate_temperature", {230, 230, 230, 230, 230, 260, 260, 265}},
        {"gate_name", nullptr},
        {"ttg_map", {0, 1, 2, 3, 4, 5, 6, 7}},
        {"endless_spool_groups", {0, 1, 2, 3, 4, 5, 6, 7}},
        {"bowden_progress", -1},
        {"encoder", nullptr},
        {"unit_gate_counts", nullptr},
        {"sync_drive", true},
        {"sync_feedback_state", "neutral"},
        {"clog_detection_enabled", 0},
        {"espooler_active", ""},
        {"spoolman_support", "push"},
        {"drying_state", {"", "", "", "", "", "", "", ""}},
        {"sensors", {
            {"mmu_pre_gate", true},
            {"mmu_gear", true},
            {"filament_proportional", false},
            {"extruder", true},
            {"toolhead", true}
        }}
    };
    test_parse_mmu_state(mmu_data);

    auto info = get_system_info();

    // Structure
    REQUIRE(info.total_slots == 8);
    REQUIRE(info.units.size() == 1);
    REQUIRE(info.units[0].slot_count == 8);

    // Current state
    REQUIRE(info.current_slot == 0);
    REQUIRE(info.current_tool == 0);
    REQUIRE(info.filament_loaded == true);
    REQUIRE(info.action == AmsAction::IDLE);
    REQUIRE(info.supports_bypass == true);

    // Colors (float arrays converted to 0xRRGGBB)
    REQUIRE(info.units[0].slots[0].color_rgb == 0xFFFFFF);  // White
    REQUIRE(info.units[0].slots[1].color_rgb == 0x000000);  // Black
    REQUIRE(info.units[0].slots[3].color_rgb == 0x042F56);  // Navy

    // Materials
    REQUIRE(info.units[0].slots[0].material == "PLA");
    REQUIRE(info.units[0].slots[5].material == "ABS");
    REQUIRE(info.units[0].slots[7].material == "ASA CF");

    // Filament names (from gate_filament_name, not gate_name which is null)
    REQUIRE(info.units[0].slots[0].color_name == "Matte White");
    REQUIRE(info.units[0].slots[3].color_name == "Matte Navy");

    // Temperatures
    REQUIRE(info.units[0].slots[0].nozzle_temp_min == 230);
    REQUIRE(info.units[0].slots[5].nozzle_temp_min == 260);

    // Sensors (aggregate format)
    REQUIRE(info.units[0].has_slot_sensors == true);

    // Dryer (array format = supported but inactive)
    auto dryer = get_dryer_info();
    REQUIRE(dryer.supported == true);
    REQUIRE(dryer.active == false);

    // v4 fields
    REQUIRE(info.sync_drive == true);
    REQUIRE(info.sync_feedback_state == "neutral");
    REQUIRE(info.spoolman_mode == SpoolmanMode::PUSH);
    REQUIRE(info.encoder_flow_rate == -1);  // null encoder
}
```

**Step 2: Run tests to verify they pass**

Run: `./build/bin/helix-tests "[emu]"`
Expected: ALL PASS (this validates all previous fixes work together)

**Step 3: Commit**

```
git add tests/unit/test_ams_backend_happy_hare.cpp
git commit -m "test(ams): add full EMU integration test with real Moonraker data"
```

---

### Task 7: Code Review

Review all changes for:
- No regressions to existing ERCF/Tradrack parsing (run full test suite)
- Correct float-to-int rounding in color conversion
- Thread safety (all parsing is on the update thread, no new threading concerns)
- No unnecessary complexity added

Run: `make test-run` (full suite, not just [emu] tag)
Expected: ALL tests pass including existing Happy Hare tests

---

### Reference: EMU Configuration Summary

From the user's Moonraker dump:
- `mmu_vendor: EMU`, `display_name: EMU`
- `selector_type: VirtualSelector` (passive combiner, no physical selector)
- `happy_hare_version: 3.42`
- 8 gates, 8 pre-gate sensors, 8 post-gear sensors
- Per-gate BME280 temperature/humidity sensors (Lane_0 through Lane_7)
- Per-gate fans (`_emu_fan_0` through `_emu_fan_7`)
- Analog sync feedback (not encoder-based)
- Separate MCU per gate (`mmu0` through `mmu7`)
