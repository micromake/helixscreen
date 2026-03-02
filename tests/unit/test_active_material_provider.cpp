// SPDX-License-Identifier: GPL-3.0-or-later
#include "active_material_provider.h"
#include "ams_types.h"
#include "filament_database.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

// ============================================================================
// build_active_material() — Unit tests (no singletons needed)
// ============================================================================

TEST_CASE("build_active_material: slot with known material uses DB temps",
          "[active_material][build]") {
    SlotInfo slot;
    slot.material = "PETG";
    slot.color_rgb = 0x00FF00;
    slot.brand = "Polymaker";

    auto result = build_active_material(slot);

    // Should use filament DB values for PETG (230-260°C nozzle, 80°C bed)
    CHECK(result.material_info.nozzle_min == 230);
    CHECK(result.material_info.nozzle_max == 260);
    CHECK(result.material_info.bed_temp == 80);
    CHECK(result.color_rgb == 0x00FF00);
    CHECK(result.brand == "Polymaker");
    CHECK(result.material_name == "PETG");
    CHECK(result.display_name == "Polymaker PETG");
}

TEST_CASE("build_active_material: slot with explicit temps overrides DB",
          "[active_material][build]") {
    SlotInfo slot;
    slot.material = "PETG";
    slot.nozzle_temp_min = 240;
    slot.nozzle_temp_max = 255;
    slot.bed_temp = 85;
    slot.color_rgb = 0xFF0000;

    auto result = build_active_material(slot);

    CHECK(result.material_info.nozzle_min == 240);
    CHECK(result.material_info.nozzle_max == 255);
    CHECK(result.material_info.bed_temp == 85);
}

TEST_CASE("build_active_material: unknown material with temps uses slot temps",
          "[active_material][build]") {
    SlotInfo slot;
    slot.material = "SuperCustomFilament";
    slot.nozzle_temp_min = 275;
    slot.nozzle_temp_max = 295;
    slot.bed_temp = 100;
    slot.color_rgb = 0x0000FF;

    auto result = build_active_material(slot);

    CHECK(result.material_info.nozzle_min == 275);
    CHECK(result.material_info.nozzle_max == 295);
    CHECK(result.material_info.bed_temp == 100);
    CHECK(result.material_name == "SuperCustomFilament");
}

TEST_CASE("build_active_material: unknown material without temps uses default",
          "[active_material][build]") {
    SlotInfo slot;
    slot.material = "UnknownStuff";
    slot.color_rgb = 0x808080;

    auto result = build_active_material(slot);

    CHECK(result.material_info.nozzle_min == 220); // DEFAULT_LOAD_PREHEAT_TEMP
    CHECK(result.material_info.nozzle_max == 220);
}

TEST_CASE("build_active_material: empty material uses default temp",
          "[active_material][build]") {
    SlotInfo slot;
    slot.color_rgb = 0x808080;

    auto result = build_active_material(slot);

    CHECK(result.material_info.nozzle_min == 220);
}

TEST_CASE("build_active_material: display_name includes brand when present",
          "[active_material][build]") {
    SlotInfo slot;
    slot.material = "PLA";
    slot.brand = "eSUN";

    auto result = build_active_material(slot);
    CHECK(result.display_name == "eSUN PLA");
}

TEST_CASE("build_active_material: display_name is material only when no brand",
          "[active_material][build]") {
    SlotInfo slot;
    slot.material = "PLA";

    auto result = build_active_material(slot);
    CHECK(result.display_name == "PLA");
}

TEST_CASE("build_active_material: spoolman IDs are carried through",
          "[active_material][build]") {
    SlotInfo slot;
    slot.material = "PLA";
    slot.spoolman_id = 42;
    slot.spoolman_filament_id = 7;
    slot.spoolman_vendor_id = 3;

    auto result = build_active_material(slot);
    CHECK(result.spoolman_id == 42);
    CHECK(result.spoolman_filament_id == 7);
    CHECK(result.spoolman_vendor_id == 3);
}

TEST_CASE("build_active_material: material alias resolved",
          "[active_material][build]") {
    SlotInfo slot;
    slot.material = "Nylon"; // Alias for PA

    auto result = build_active_material(slot);

    // Should resolve to PA temps (250-280°C nozzle)
    CHECK(result.material_info.nozzle_min == 250);
    CHECK(result.material_info.nozzle_max == 280);
    CHECK(result.material_name == "Nylon"); // Keep original name for display
}

// ============================================================================
// Spool preset matching logic tests
// ============================================================================

TEST_CASE("build_active_material: nozzle_recommended() computes midpoint",
          "[active_material][preset]") {
    SlotInfo slot;
    slot.material = "PETG"; // DB: 230-260°C

    auto result = build_active_material(slot);
    CHECK(result.material_info.nozzle_recommended() == 245); // (230+260)/2
}

TEST_CASE("build_active_material: overridden temps affect nozzle_recommended()",
          "[active_material][preset]") {
    SlotInfo slot;
    slot.material = "PETG";
    slot.nozzle_temp_min = 240;
    slot.nozzle_temp_max = 260;

    auto result = build_active_material(slot);
    CHECK(result.material_info.nozzle_recommended() == 250); // (240+260)/2
}

TEST_CASE("build_active_material: partial temp override (min only) preserves DB max",
          "[active_material][preset]") {
    SlotInfo slot;
    slot.material = "PLA";
    slot.nozzle_temp_min = 200; // Override min only, DB max stays 220

    auto result = build_active_material(slot);
    CHECK(result.material_info.nozzle_min == 200);
    CHECK(result.material_info.nozzle_max == 220); // From DB
    CHECK(result.material_info.nozzle_recommended() == 210);
}

TEST_CASE("build_active_material: synthetic material has correct bed_temp default",
          "[active_material][preset]") {
    SlotInfo slot;
    slot.material = "MysteryFilament";
    // No temps set

    auto result = build_active_material(slot);
    CHECK(result.material_info.bed_temp == 60); // Sensible default
}

TEST_CASE("build_active_material: case-insensitive material lookup",
          "[active_material][preset]") {
    SlotInfo slot;
    slot.material = "petg"; // lowercase

    auto result = build_active_material(slot);
    CHECK(result.material_info.nozzle_min == 230); // Still finds PETG
    CHECK(result.material_info.bed_temp == 80);
}

TEST_CASE("build_active_material: display_name with empty brand and material",
          "[active_material][preset]") {
    SlotInfo slot;
    // Both empty

    auto result = build_active_material(slot);
    CHECK(result.display_name == "Unknown");
}
