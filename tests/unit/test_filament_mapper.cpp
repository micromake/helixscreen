// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_filament_mapper.cpp
 * @brief Unit tests for FilamentMapper — pure logic, no LVGL dependency
 *
 * Tests:
 * - color_distance() weighted RGB metric
 * - colors_match() tolerance boundary
 * - materials_match() case-insensitive comparison
 * - find_closest_color_slot() slot selection with SlotKey
 * - compute_defaults() full mapping pipeline
 * - Multi-backend slot uniqueness
 */

#include "filament_mapper.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

// =============================================================================
// color_distance
// =============================================================================

TEST_CASE("color_distance returns 0 for identical colors", "[filament_mapper][color]") {
    CHECK(FilamentMapper::color_distance(0x000000, 0x000000) == 0);
    CHECK(FilamentMapper::color_distance(0xFF0000, 0xFF0000) == 0);
    CHECK(FilamentMapper::color_distance(0xABCDEF, 0xABCDEF) == 0);
}

TEST_CASE("color_distance is symmetric", "[filament_mapper][color]") {
    CHECK(FilamentMapper::color_distance(0xFF0000, 0x00FF00) ==
          FilamentMapper::color_distance(0x00FF00, 0xFF0000));
    CHECK(FilamentMapper::color_distance(0x123456, 0x654321) ==
          FilamentMapper::color_distance(0x654321, 0x123456));
}

TEST_CASE("color_distance uses luminance weighting", "[filament_mapper][color]") {
    // Pure green difference should weigh more than pure blue
    int green_diff = FilamentMapper::color_distance(0x000000, 0x001000);
    int blue_diff = FilamentMapper::color_distance(0x000000, 0x000010);
    CHECK(green_diff >= blue_diff);
}

TEST_CASE("color_distance max is for black vs white", "[filament_mapper][color]") {
    int max_dist = FilamentMapper::color_distance(0x000000, 0xFFFFFF);
    CHECK(max_dist > 200);
    CHECK(max_dist < 300); // Weighted, so less than 441 (unweighted Euclidean)
}

TEST_CASE("color_distance per-channel ranges", "[filament_mapper][color][edge]") {
    SECTION("only red channel differs") {
        int dist = FilamentMapper::color_distance(0x000000, 0xFF0000);
        // sqrt(255^2 * 30 / 100) ~ 139
        CHECK(dist > 130);
        CHECK(dist < 150);
    }

    SECTION("only green channel differs") {
        int dist = FilamentMapper::color_distance(0x000000, 0x00FF00);
        // sqrt(255^2 * 59 / 100) ~ 195
        CHECK(dist > 190);
        CHECK(dist < 200);
    }

    SECTION("only blue channel differs") {
        int dist = FilamentMapper::color_distance(0x000000, 0x0000FF);
        // sqrt(255^2 * 11 / 100) ~ 84
        CHECK(dist > 80);
        CHECK(dist < 90);
    }
}

// =============================================================================
// colors_match
// =============================================================================

TEST_CASE("colors_match tolerance boundary", "[filament_mapper][color]") {
    CHECK(FilamentMapper::colors_match(0xFF0000, 0xFF0000));

    SECTION("known under-tolerance pair matches") {
        // Red shift of 15: sqrt(15^2 * 30 / 100) = sqrt(67.5) ~ 8
        CHECK(FilamentMapper::colors_match(0x800000, 0x8F0000));
    }

    SECTION("known over-tolerance pair does not match") {
        // Red vs green: distance >> 40
        CHECK_FALSE(FilamentMapper::colors_match(0xFF0000, 0x00FF00));
        CHECK_FALSE(FilamentMapper::colors_match(0x000000, 0xFFFFFF));
    }

    SECTION("slightly different colors match") {
        CHECK(FilamentMapper::colors_match(0xFF0000, 0xF00000));
        CHECK(FilamentMapper::colors_match(0x00FF00, 0x00F000));
    }
}

// =============================================================================
// materials_match (case-insensitive)
// =============================================================================

TEST_CASE("materials_match is case-insensitive", "[filament_mapper][material]") {
    CHECK(FilamentMapper::materials_match("PLA", "PLA"));
    CHECK(FilamentMapper::materials_match("PLA", "pla"));
    CHECK(FilamentMapper::materials_match("Pla", "pLA"));
    CHECK(FilamentMapper::materials_match("PETG", "petg"));
    CHECK_FALSE(FilamentMapper::materials_match("PLA", "PETG"));
    CHECK_FALSE(FilamentMapper::materials_match("PLA", "PLA+"));
    CHECK(FilamentMapper::materials_match("", ""));
}

// =============================================================================
// find_closest_color_slot (now returns SlotKey)
// =============================================================================

TEST_CASE("find_closest_color_slot with no slots returns invalid key", "[filament_mapper][slot]") {
    std::vector<AvailableSlot> slots;
    std::vector<SlotKey> used;
    auto result = FilamentMapper::find_closest_color_slot(0xFF0000, slots, used);
    CHECK(result == SlotKey{-1, -1});
}

TEST_CASE("find_closest_color_slot skips empty slots", "[filament_mapper][slot]") {
    std::vector<AvailableSlot> slots = {
        {0, 0, 0xFF0000, "PLA", true, -1},  // empty
        {1, 0, 0x00FF00, "PLA", false, -1}, // green, not empty
    };
    std::vector<SlotKey> used;

    // Looking for red — slot 0 matches color but is empty
    auto result = FilamentMapper::find_closest_color_slot(0xFF0000, slots, used);
    CHECK(result == SlotKey{-1, -1});
}

TEST_CASE("find_closest_color_slot skips already-used slots", "[filament_mapper][slot]") {
    std::vector<AvailableSlot> slots = {
        {0, 0, 0xFF0000, "PLA", false, -1},
        {1, 0, 0xFF1010, "PLA", false, -1},
    };
    std::vector<SlotKey> used = {{0, 0}}; // slot 0 backend 0 already used

    auto result = FilamentMapper::find_closest_color_slot(0xFF0000, slots, used);
    CHECK(result == SlotKey{1, 0});
}

TEST_CASE("find_closest_color_slot returns closest match", "[filament_mapper][slot]") {
    std::vector<AvailableSlot> slots = {
        {0, 0, 0xFF0000, "PLA", false, -1}, // exact red
        {1, 0, 0xF00000, "PLA", false, -1}, // slightly off red
        {2, 0, 0x00FF00, "PLA", false, -1}, // green (far)
    };
    std::vector<SlotKey> used;

    auto result = FilamentMapper::find_closest_color_slot(0xFF0000, slots, used);
    CHECK(result == SlotKey{0, 0});
}

TEST_CASE("find_closest_color_slot returns invalid key when nothing within tolerance",
          "[filament_mapper][slot]") {
    std::vector<AvailableSlot> slots = {
        {0, 0, 0x00FF00, "PLA", false, -1},
        {1, 0, 0x0000FF, "PLA", false, -1},
    };
    std::vector<SlotKey> used;

    auto result = FilamentMapper::find_closest_color_slot(0xFF0000, slots, used);
    CHECK(result == SlotKey{-1, -1});
}

// =============================================================================
// compute_defaults — empty inputs
// =============================================================================

TEST_CASE("compute_defaults with empty inputs", "[filament_mapper][compute]") {
    SECTION("no tools, no slots") {
        auto result = FilamentMapper::compute_defaults({}, {});
        CHECK(result.empty());
    }

    SECTION("tools but no slots") {
        std::vector<GcodeToolInfo> tools = {{0, 0xFF0000, "PLA"}};
        auto result = FilamentMapper::compute_defaults(tools, {});
        REQUIRE(result.size() == 1);
        CHECK(result[0].is_auto);
        CHECK(result[0].reason == ToolMapping::MatchReason::AUTO);
        CHECK(result[0].mapped_slot == -1);
    }

    SECTION("no tools but has slots") {
        std::vector<AvailableSlot> slots = {{0, 0, 0xFF0000, "PLA", false, -1}};
        auto result = FilamentMapper::compute_defaults({}, slots);
        CHECK(result.empty());
    }
}

// =============================================================================
// compute_defaults — single tool
// =============================================================================

TEST_CASE("compute_defaults single tool single slot basic match", "[filament_mapper][compute]") {
    std::vector<GcodeToolInfo> tools = {{0, 0xFF0000, "PLA"}};
    std::vector<AvailableSlot> slots = {{0, 0, 0xFF0000, "PLA", false, -1}};

    auto result = FilamentMapper::compute_defaults(tools, slots);
    REQUIRE(result.size() == 1);
    CHECK(result[0].tool_index == 0);
    CHECK(result[0].mapped_slot == 0);
    CHECK(result[0].mapped_backend == 0);
    CHECK_FALSE(result[0].material_mismatch);
    CHECK_FALSE(result[0].is_auto);
    CHECK(result[0].reason == ToolMapping::MatchReason::COLOR_MATCH);
}

// =============================================================================
// compute_defaults — firmware mapping
// =============================================================================

TEST_CASE("compute_defaults firmware mapping is preferred over color match",
          "[filament_mapper][compute][firmware]") {
    std::vector<GcodeToolInfo> tools = {{0, 0xFF0000, "PLA"}};

    std::vector<AvailableSlot> slots = {
        {0, 0, 0xFF0000, "PLA", false, -1},
        {1, 0, 0x00FF00, "PLA", false, 0}, // firmware maps to tool 0
    };

    auto result = FilamentMapper::compute_defaults(tools, slots);
    REQUIRE(result.size() == 1);
    CHECK(result[0].mapped_slot == 1);
    CHECK(result[0].reason == ToolMapping::MatchReason::FIRMWARE_MAPPING);
}

TEST_CASE("compute_defaults firmware mapping detects material mismatch",
          "[filament_mapper][compute][firmware]") {
    std::vector<GcodeToolInfo> tools = {{0, 0xFF0000, "PLA"}};
    std::vector<AvailableSlot> slots = {
        {0, 0, 0xFF0000, "PETG", false, 0},
    };

    auto result = FilamentMapper::compute_defaults(tools, slots);
    REQUIRE(result.size() == 1);
    CHECK(result[0].reason == ToolMapping::MatchReason::FIRMWARE_MAPPING);
    CHECK(result[0].material_mismatch);
}

TEST_CASE("compute_defaults firmware mapping ignores empty slots",
          "[filament_mapper][compute][firmware]") {
    std::vector<GcodeToolInfo> tools = {{0, 0xFF0000, "PLA"}};
    std::vector<AvailableSlot> slots = {
        {0, 0, 0xFF0000, "PLA", true, 0}, // firmware-mapped but empty
    };

    auto result = FilamentMapper::compute_defaults(tools, slots);
    REQUIRE(result.size() == 1);
    CHECK(result[0].is_auto);
    CHECK(result[0].reason == ToolMapping::MatchReason::AUTO);
}

TEST_CASE("compute_defaults duplicate firmware mapping takes first non-empty",
          "[filament_mapper][compute][firmware]") {
    std::vector<GcodeToolInfo> tools = {{0, 0xFF0000, "PLA"}};
    std::vector<AvailableSlot> slots = {
        {0, 0, 0xFF0000, "PLA", false, 0}, // both claim tool 0
        {1, 0, 0x00FF00, "PLA", false, 0},
    };

    auto result = FilamentMapper::compute_defaults(tools, slots);
    REQUIRE(result.size() == 1);
    CHECK(result[0].mapped_slot == 0); // first one wins
    CHECK(result[0].reason == ToolMapping::MatchReason::FIRMWARE_MAPPING);
}

// =============================================================================
// compute_defaults — color matching
// =============================================================================

TEST_CASE("compute_defaults color match with material mismatch",
          "[filament_mapper][compute][color]") {
    std::vector<GcodeToolInfo> tools = {{0, 0xFF0000, "PLA"}};
    std::vector<AvailableSlot> slots = {
        {0, 0, 0xFF0000, "PETG", false, -1},
    };

    auto result = FilamentMapper::compute_defaults(tools, slots);
    REQUIRE(result.size() == 1);
    CHECK(result[0].mapped_slot == 0);
    CHECK(result[0].reason == ToolMapping::MatchReason::COLOR_MATCH);
    CHECK(result[0].material_mismatch);
}

TEST_CASE("compute_defaults case-insensitive material match no mismatch",
          "[filament_mapper][compute][material]") {
    std::vector<GcodeToolInfo> tools = {{0, 0xFF0000, "pla"}}; // lowercase
    std::vector<AvailableSlot> slots = {
        {0, 0, 0xFF0000, "PLA", false, -1}, // uppercase
    };

    auto result = FilamentMapper::compute_defaults(tools, slots);
    REQUIRE(result.size() == 1);
    CHECK_FALSE(result[0].material_mismatch);
}

TEST_CASE("compute_defaults no color match falls through to auto",
          "[filament_mapper][compute][color]") {
    std::vector<GcodeToolInfo> tools = {{0, 0xFF0000, "PLA"}};
    std::vector<AvailableSlot> slots = {
        {0, 0, 0x00FF00, "PLA", false, -1},
    };

    auto result = FilamentMapper::compute_defaults(tools, slots);
    REQUIRE(result.size() == 1);
    CHECK(result[0].is_auto);
    CHECK(result[0].reason == ToolMapping::MatchReason::AUTO);
    CHECK(result[0].mapped_slot == -1);
}

// =============================================================================
// compute_defaults — multi-tool
// =============================================================================

TEST_CASE("compute_defaults multi-tool no conflicts", "[filament_mapper][compute][multi]") {
    std::vector<GcodeToolInfo> tools = {
        {0, 0xFF0000, "PLA"},
        {1, 0x00FF00, "PLA"},
        {2, 0x0000FF, "PLA"},
    };
    std::vector<AvailableSlot> slots = {
        {0, 0, 0xFF0000, "PLA", false, -1},
        {1, 0, 0x00FF00, "PLA", false, -1},
        {2, 0, 0x0000FF, "PLA", false, -1},
    };

    auto result = FilamentMapper::compute_defaults(tools, slots);
    REQUIRE(result.size() == 3);

    CHECK(result[0].mapped_slot == 0);
    CHECK(result[1].mapped_slot == 1);
    CHECK(result[2].mapped_slot == 2);

    for (const auto& m : result) {
        CHECK(m.reason == ToolMapping::MatchReason::COLOR_MATCH);
        CHECK_FALSE(m.material_mismatch);
        CHECK_FALSE(m.is_auto);
    }
}

TEST_CASE("compute_defaults multi-tool with color conflict",
          "[filament_mapper][compute][multi]") {
    std::vector<GcodeToolInfo> tools = {
        {0, 0xFF0000, "PLA"},
        {1, 0xFF0000, "PLA"}, // same color
    };
    std::vector<AvailableSlot> slots = {
        {0, 0, 0xFF0000, "PLA", false, -1},
        {1, 0, 0xF00000, "PLA", false, -1}, // slightly off red
    };

    auto result = FilamentMapper::compute_defaults(tools, slots);
    REQUIRE(result.size() == 2);

    CHECK(result[0].mapped_slot == 0);
    CHECK(result[1].mapped_slot == 1); // next best since slot 0 is claimed
}

TEST_CASE("compute_defaults multi-tool conflict exhausts slots",
          "[filament_mapper][compute][multi]") {
    std::vector<GcodeToolInfo> tools = {
        {0, 0xFF0000, "PLA"},
        {1, 0xFF0000, "PLA"},
    };
    std::vector<AvailableSlot> slots = {
        {0, 0, 0xFF0000, "PLA", false, -1},
        {1, 0, 0x00FF00, "PLA", false, -1}, // green, too far
    };

    auto result = FilamentMapper::compute_defaults(tools, slots);
    REQUIRE(result.size() == 2);

    CHECK(result[0].mapped_slot == 0);
    CHECK(result[1].is_auto);
    CHECK(result[1].mapped_slot == -1);
}

// =============================================================================
// compute_defaults — all empty slots
// =============================================================================

TEST_CASE("compute_defaults all empty slots", "[filament_mapper][compute]") {
    std::vector<GcodeToolInfo> tools = {{0, 0xFF0000, "PLA"}, {1, 0x00FF00, "PLA"}};
    std::vector<AvailableSlot> slots = {
        {0, 0, 0xFF0000, "PLA", true, -1},
        {1, 0, 0x00FF00, "PLA", true, -1},
    };

    auto result = FilamentMapper::compute_defaults(tools, slots);
    REQUIRE(result.size() == 2);
    for (const auto& m : result) {
        CHECK(m.is_auto);
        CHECK(m.mapped_slot == -1);
    }
}

// =============================================================================
// compute_defaults — mixed scenarios
// =============================================================================

TEST_CASE("compute_defaults mixed firmware, color, and auto",
          "[filament_mapper][compute][mixed]") {
    std::vector<GcodeToolInfo> tools = {
        {0, 0xFF0000, "PLA"},
        {1, 0x00FF00, "PLA"},
        {2, 0x0000FF, "PETG"},
    };
    std::vector<AvailableSlot> slots = {
        {0, 0, 0xFF0000, "PLA", false, 0},
        {1, 0, 0x00FF00, "PLA", false, -1},
        {2, 0, 0xFFFF00, "ABS", false, -1}, // yellow, won't match blue
    };

    auto result = FilamentMapper::compute_defaults(tools, slots);
    REQUIRE(result.size() == 3);

    CHECK(result[0].mapped_slot == 0);
    CHECK(result[0].reason == ToolMapping::MatchReason::FIRMWARE_MAPPING);

    CHECK(result[1].mapped_slot == 1);
    CHECK(result[1].reason == ToolMapping::MatchReason::COLOR_MATCH);

    CHECK(result[2].is_auto);
    CHECK(result[2].mapped_slot == -1);
}

TEST_CASE("compute_defaults empty material strings skip mismatch check",
          "[filament_mapper][compute]") {
    SECTION("empty tool material") {
        std::vector<GcodeToolInfo> tools = {{0, 0xFF0000, ""}};
        std::vector<AvailableSlot> slots = {{0, 0, 0xFF0000, "PLA", false, -1}};
        auto result = FilamentMapper::compute_defaults(tools, slots);
        CHECK_FALSE(result[0].material_mismatch);
    }

    SECTION("empty slot material") {
        std::vector<GcodeToolInfo> tools = {{0, 0xFF0000, "PLA"}};
        std::vector<AvailableSlot> slots = {{0, 0, 0xFF0000, "", false, -1}};
        auto result = FilamentMapper::compute_defaults(tools, slots);
        CHECK_FALSE(result[0].material_mismatch);
    }
}

// =============================================================================
// compute_defaults — backend index propagation
// =============================================================================

TEST_CASE("compute_defaults propagates backend index", "[filament_mapper][compute]") {
    std::vector<GcodeToolInfo> tools = {{0, 0xFF0000, "PLA"}};
    std::vector<AvailableSlot> slots = {
        {0, 2, 0xFF0000, "PLA", false, -1},
    };

    auto result = FilamentMapper::compute_defaults(tools, slots);
    REQUIRE(result.size() == 1);
    CHECK(result[0].mapped_backend == 2);
}

// =============================================================================
// Multi-backend slot uniqueness (critical bug fix)
// =============================================================================

TEST_CASE("compute_defaults distinguishes same slot_index across backends",
          "[filament_mapper][compute][multi_backend]") {
    // Two backends each have slot 0 with red filament
    std::vector<GcodeToolInfo> tools = {
        {0, 0xFF0000, "PLA"}, // red
        {1, 0xFF0000, "PLA"}, // also red
    };
    std::vector<AvailableSlot> slots = {
        {0, 0, 0xFF0000, "PLA", false, -1}, // slot 0, backend 0
        {0, 1, 0xFF0000, "PLA", false, -1}, // slot 0, backend 1 (different physical slot!)
    };

    auto result = FilamentMapper::compute_defaults(tools, slots);
    REQUIRE(result.size() == 2);

    // Tool 0 gets slot 0 from backend 0
    CHECK(result[0].mapped_slot == 0);
    CHECK(result[0].mapped_backend == 0);

    // Tool 1 gets slot 0 from backend 1 (NOT auto, because the slot is available)
    CHECK(result[1].mapped_slot == 0);
    CHECK(result[1].mapped_backend == 1);
    CHECK_FALSE(result[1].is_auto);
}

TEST_CASE("compute_defaults multi-backend firmware mapping uses correct backend",
          "[filament_mapper][compute][multi_backend]") {
    std::vector<GcodeToolInfo> tools = {{0, 0xFF0000, "PLA"}};
    std::vector<AvailableSlot> slots = {
        {0, 0, 0xFF0000, "PLA", false, -1}, // same slot_index, no firmware map
        {0, 1, 0x00FF00, "PLA", false, 0},  // same slot_index, firmware-mapped to tool 0
    };

    auto result = FilamentMapper::compute_defaults(tools, slots);
    REQUIRE(result.size() == 1);
    CHECK(result[0].mapped_slot == 0);
    CHECK(result[0].mapped_backend == 1); // firmware mapping is on backend 1
    CHECK(result[0].reason == ToolMapping::MatchReason::FIRMWARE_MAPPING);
}

TEST_CASE("find_closest_color_slot distinguishes backends in used list",
          "[filament_mapper][slot][multi_backend]") {
    std::vector<AvailableSlot> slots = {
        {0, 0, 0xFF0000, "PLA", false, -1},
        {0, 1, 0xFF0000, "PLA", false, -1}, // same slot_index, different backend
    };

    // Mark slot 0 from backend 0 as used
    std::vector<SlotKey> used = {{0, 0}};

    auto result = FilamentMapper::find_closest_color_slot(0xFF0000, slots, used);
    // Should return slot 0 from backend 1 (not skip it due to slot_index match)
    CHECK(result == SlotKey{0, 1});
}
