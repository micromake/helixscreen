// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "bed_mesh_buffer.h"

#include "../catch_amalgamated.hpp"

using namespace helix::mesh;

// ============================================================================
// Construction & Dimensions
// ============================================================================

TEST_CASE("PixelBuffer: dimensions and stride", "[bed_mesh]") {
    PixelBuffer buf(100, 50);
    REQUIRE(buf.width() == 100);
    REQUIRE(buf.height() == 50);
    REQUIRE(buf.stride() == 100 * 4);
    REQUIRE(buf.data() != nullptr);
}

TEST_CASE("PixelBuffer: zero-size buffer", "[bed_mesh]") {
    PixelBuffer buf(0, 0);
    REQUIRE(buf.width() == 0);
    REQUIRE(buf.height() == 0);
    REQUIRE(buf.stride() == 0);
}

TEST_CASE("PixelBuffer: 1x1 buffer", "[bed_mesh]") {
    PixelBuffer buf(1, 1);
    REQUIRE(buf.width() == 1);
    REQUIRE(buf.height() == 1);
    REQUIRE(buf.stride() == 4);
}

// ============================================================================
// clear()
// ============================================================================

TEST_CASE("PixelBuffer: clear fills all pixels", "[bed_mesh]") {
    PixelBuffer buf(3, 2);
    buf.clear(255, 128, 64, 200);

    // Check every pixel has BGRA order
    for (int y = 0; y < 2; y++) {
        for (int x = 0; x < 3; x++) {
            const uint8_t* p = buf.pixel_at(x, y);
            REQUIRE(p != nullptr);
            CHECK(p[0] == 64);  // B
            CHECK(p[1] == 128); // G
            CHECK(p[2] == 255); // R
            CHECK(p[3] == 200); // A
        }
    }
}

TEST_CASE("PixelBuffer: clear with zeros", "[bed_mesh]") {
    PixelBuffer buf(2, 2);
    // First fill with something
    buf.clear(255, 255, 255, 255);
    // Then clear to zero
    buf.clear(0, 0, 0, 0);

    const uint8_t* p = buf.pixel_at(0, 0);
    CHECK(p[0] == 0);
    CHECK(p[1] == 0);
    CHECK(p[2] == 0);
    CHECK(p[3] == 0);
}

// ============================================================================
// set_pixel() & BGRA byte order
// ============================================================================

TEST_CASE("PixelBuffer: set_pixel writes correct BGRA values", "[bed_mesh]") {
    PixelBuffer buf(4, 4);
    buf.clear(0, 0, 0, 0);

    buf.set_pixel(2, 1, 0xAA, 0xBB, 0xCC, 0xFF);

    const uint8_t* p = buf.pixel_at(2, 1);
    CHECK(p[0] == 0xCC); // B
    CHECK(p[1] == 0xBB); // G
    CHECK(p[2] == 0xAA); // R
    CHECK(p[3] == 0xFF); // A
}

TEST_CASE("PixelBuffer: set_pixel at corners", "[bed_mesh]") {
    PixelBuffer buf(10, 10);
    buf.clear(0, 0, 0, 0);

    // Top-left
    buf.set_pixel(0, 0, 1, 2, 3, 255);
    CHECK(buf.pixel_at(0, 0)[2] == 1); // R

    // Top-right
    buf.set_pixel(9, 0, 10, 20, 30, 255);
    CHECK(buf.pixel_at(9, 0)[2] == 10);

    // Bottom-left
    buf.set_pixel(0, 9, 100, 200, 150, 255);
    CHECK(buf.pixel_at(0, 9)[2] == 100);

    // Bottom-right
    buf.set_pixel(9, 9, 50, 60, 70, 255);
    CHECK(buf.pixel_at(9, 9)[2] == 50);
}

// ============================================================================
// Out-of-bounds safety
// ============================================================================

TEST_CASE("PixelBuffer: out-of-bounds set_pixel is no-op", "[bed_mesh]") {
    PixelBuffer buf(5, 5);
    buf.clear(0, 0, 0, 0);

    // These should not crash or modify anything
    buf.set_pixel(-1, 0, 255, 255, 255, 255);
    buf.set_pixel(0, -1, 255, 255, 255, 255);
    buf.set_pixel(5, 0, 255, 255, 255, 255);
    buf.set_pixel(0, 5, 255, 255, 255, 255);
    buf.set_pixel(-100, -100, 255, 255, 255, 255);
    buf.set_pixel(1000, 1000, 255, 255, 255, 255);

    // Verify buffer is still all zeros
    for (int y = 0; y < 5; y++) {
        for (int x = 0; x < 5; x++) {
            const uint8_t* p = buf.pixel_at(x, y);
            CHECK(p[0] == 0);
            CHECK(p[1] == 0);
            CHECK(p[2] == 0);
            CHECK(p[3] == 0);
        }
    }
}

TEST_CASE("PixelBuffer: out-of-bounds pixel_at returns nullptr", "[bed_mesh]") {
    PixelBuffer buf(5, 5);
    CHECK(buf.pixel_at(-1, 0) == nullptr);
    CHECK(buf.pixel_at(0, -1) == nullptr);
    CHECK(buf.pixel_at(5, 0) == nullptr);
    CHECK(buf.pixel_at(0, 5) == nullptr);
}

TEST_CASE("PixelBuffer: operations on zero-size buffer", "[bed_mesh]") {
    PixelBuffer buf(0, 0);
    // None of these should crash
    buf.clear(255, 255, 255, 255);
    buf.set_pixel(0, 0, 255, 0, 0, 255);
    buf.fill_hline(0, 10, 0, 255, 0, 0, 255);
    buf.draw_line(0, 0, 5, 5, 255, 0, 0, 255);
    CHECK(buf.pixel_at(0, 0) == nullptr);
}

// ============================================================================
// fill_hline()
// ============================================================================

TEST_CASE("PixelBuffer: fill_hline draws correct span", "[bed_mesh]") {
    PixelBuffer buf(10, 5);
    buf.clear(0, 0, 0, 0);

    // Fill 4 pixels starting at x=2, y=1
    buf.fill_hline(2, 4, 1, 255, 128, 64, 255);

    // Pixels before the span should be untouched
    CHECK(buf.pixel_at(1, 1)[2] == 0); // R

    // Pixels in the span should be set
    for (int x = 2; x < 6; x++) {
        const uint8_t* p = buf.pixel_at(x, 1);
        CHECK(p[0] == 64);  // B
        CHECK(p[1] == 128); // G
        CHECK(p[2] == 255); // R
        CHECK(p[3] == 255); // A
    }

    // Pixels after the span should be untouched
    CHECK(buf.pixel_at(6, 1)[2] == 0);
}

TEST_CASE("PixelBuffer: fill_hline clamped to left edge", "[bed_mesh]") {
    PixelBuffer buf(10, 5);
    buf.clear(0, 0, 0, 0);

    // Start at x=-3, width=5 => should draw pixels at x=0,1
    buf.fill_hline(-3, 5, 2, 100, 0, 0, 255);

    CHECK(buf.pixel_at(0, 2)[2] == 100); // R
    CHECK(buf.pixel_at(1, 2)[2] == 100);
    CHECK(buf.pixel_at(2, 2)[2] == 0); // Beyond the span
}

TEST_CASE("PixelBuffer: fill_hline clamped to right edge", "[bed_mesh]") {
    PixelBuffer buf(10, 5);
    buf.clear(0, 0, 0, 0);

    // Start at x=8, width=5 => should draw pixels at x=8,9
    buf.fill_hline(8, 5, 2, 100, 0, 0, 255);

    CHECK(buf.pixel_at(7, 2)[2] == 0); // Before span
    CHECK(buf.pixel_at(8, 2)[2] == 100);
    CHECK(buf.pixel_at(9, 2)[2] == 100);
}

TEST_CASE("PixelBuffer: fill_hline out-of-bounds Y is no-op", "[bed_mesh]") {
    PixelBuffer buf(10, 5);
    buf.clear(0, 0, 0, 0);

    buf.fill_hline(0, 10, -1, 255, 0, 0, 255);
    buf.fill_hline(0, 10, 5, 255, 0, 0, 255);

    // Nothing should be drawn
    for (int x = 0; x < 10; x++) {
        CHECK(buf.pixel_at(x, 0)[2] == 0);
        CHECK(buf.pixel_at(x, 4)[2] == 0);
    }
}

TEST_CASE("PixelBuffer: fill_hline zero or negative width is no-op", "[bed_mesh]") {
    PixelBuffer buf(10, 5);
    buf.clear(0, 0, 0, 0);

    buf.fill_hline(0, 0, 2, 255, 0, 0, 255);
    buf.fill_hline(0, -5, 2, 255, 0, 0, 255);

    for (int x = 0; x < 10; x++) {
        CHECK(buf.pixel_at(x, 2)[2] == 0);
    }
}

// ============================================================================
// Alpha blending
// ============================================================================

TEST_CASE("PixelBuffer: fill_hline alpha blending", "[bed_mesh]") {
    PixelBuffer buf(5, 1);
    // Set background to solid white
    buf.clear(255, 255, 255, 255);

    // Draw 50% transparent red over it
    // result = (src * alpha + dst * (255 - alpha)) / 255
    // R: (255 * 128 + 255 * 127) / 255 = 255
    // G: (0 * 128 + 255 * 127) / 255 = 127
    // B: (0 * 128 + 255 * 127) / 255 = 127
    buf.fill_hline(0, 5, 0, 255, 0, 0, 128);

    const uint8_t* p = buf.pixel_at(0, 0);
    // Allow +/- 1 for integer rounding
    CHECK(p[2] >= 254); // R: ~255
    CHECK(p[1] <= 128); // G: ~127
    CHECK(p[0] <= 128); // B: ~127
}

TEST_CASE("PixelBuffer: set_pixel alpha=0 is no-op", "[bed_mesh]") {
    PixelBuffer buf(5, 5);
    buf.clear(100, 100, 100, 255);

    buf.set_pixel(2, 2, 255, 0, 0, 0);

    const uint8_t* p = buf.pixel_at(2, 2);
    CHECK(p[2] == 100); // R unchanged
    CHECK(p[1] == 100); // G unchanged
    CHECK(p[0] == 100); // B unchanged
}

TEST_CASE("PixelBuffer: set_pixel alpha=255 overwrites completely", "[bed_mesh]") {
    PixelBuffer buf(5, 5);
    buf.clear(100, 100, 100, 255);

    buf.set_pixel(2, 2, 200, 150, 50, 255);

    const uint8_t* p = buf.pixel_at(2, 2);
    CHECK(p[2] == 200); // R
    CHECK(p[1] == 150); // G
    CHECK(p[0] == 50);  // B
    CHECK(p[3] == 255); // A
}

TEST_CASE("PixelBuffer: alpha blend mid-value accuracy", "[bed_mesh]") {
    PixelBuffer buf(1, 1);
    // Background: R=0, G=0, B=0, A=255
    buf.clear(0, 0, 0, 255);

    // Blend with alpha=128: result = (200 * 128 + 0 * 127) / 255 = ~100
    buf.set_pixel(0, 0, 200, 100, 50, 128);

    const uint8_t* p = buf.pixel_at(0, 0);
    // (200 * 128) / 255 = ~100
    CHECK(p[2] >= 99);
    CHECK(p[2] <= 101);
    // (100 * 128) / 255 = ~50
    CHECK(p[1] >= 49);
    CHECK(p[1] <= 51);
    // (50 * 128) / 255 = ~25
    CHECK(p[0] >= 24);
    CHECK(p[0] <= 26);
}

// ============================================================================
// draw_line()
// ============================================================================

TEST_CASE("PixelBuffer: draw_line horizontal", "[bed_mesh]") {
    PixelBuffer buf(20, 10);
    buf.clear(0, 0, 0, 0);

    buf.draw_line(2, 5, 12, 5, 255, 0, 0, 255);

    // Check that pixels along the line are set
    CHECK(buf.pixel_at(2, 5)[2] == 255);  // R at start
    CHECK(buf.pixel_at(7, 5)[2] == 255);  // R at middle
    CHECK(buf.pixel_at(12, 5)[2] == 255); // R at end

    // Check a pixel off the line is not set
    CHECK(buf.pixel_at(7, 3)[2] == 0);
}

TEST_CASE("PixelBuffer: draw_line vertical", "[bed_mesh]") {
    PixelBuffer buf(10, 20);
    buf.clear(0, 0, 0, 0);

    buf.draw_line(5, 2, 5, 12, 0, 255, 0, 255);

    CHECK(buf.pixel_at(5, 2)[1] == 255);  // G at start
    CHECK(buf.pixel_at(5, 7)[1] == 255);  // G at middle
    CHECK(buf.pixel_at(5, 12)[1] == 255); // G at end

    // Off the line
    CHECK(buf.pixel_at(3, 7)[1] == 0);
}

TEST_CASE("PixelBuffer: draw_line diagonal", "[bed_mesh]") {
    PixelBuffer buf(20, 20);
    buf.clear(0, 0, 0, 0);

    buf.draw_line(0, 0, 10, 10, 255, 255, 255, 255);

    // Diagonal line should hit the start and end
    CHECK(buf.pixel_at(0, 0)[2] == 255);
    CHECK(buf.pixel_at(10, 10)[2] == 255);

    // And some point along the diagonal
    CHECK(buf.pixel_at(5, 5)[2] == 255);
}

TEST_CASE("PixelBuffer: draw_line single point", "[bed_mesh]") {
    PixelBuffer buf(10, 10);
    buf.clear(0, 0, 0, 0);

    buf.draw_line(5, 5, 5, 5, 255, 0, 0, 255);

    CHECK(buf.pixel_at(5, 5)[2] == 255);
}

TEST_CASE("PixelBuffer: draw_line with thickness", "[bed_mesh]") {
    PixelBuffer buf(20, 20);
    buf.clear(0, 0, 0, 0);

    // Horizontal line with thickness 3 at y=10
    buf.draw_line(2, 10, 15, 10, 255, 0, 0, 255, 3);

    // Center line
    CHECK(buf.pixel_at(8, 10)[2] == 255);
    // One pixel above and below should also be drawn
    CHECK(buf.pixel_at(8, 9)[2] == 255);
    CHECK(buf.pixel_at(8, 11)[2] == 255);
    // Two pixels away should not
    CHECK(buf.pixel_at(8, 8)[2] == 0);
    CHECK(buf.pixel_at(8, 12)[2] == 0);
}

TEST_CASE("PixelBuffer: draw_line clipped to bounds", "[bed_mesh]") {
    PixelBuffer buf(10, 10);
    buf.clear(0, 0, 0, 0);

    // Line that extends well outside bounds -- should not crash
    buf.draw_line(-50, -50, 50, 50, 255, 0, 0, 255);

    // Some pixel on the diagonal within bounds should be set
    CHECK(buf.pixel_at(5, 5)[2] == 255);
}

// ============================================================================
// fill_triangle_solid()
// ============================================================================

TEST_CASE("BedMeshBuffer solid triangle fill", "[bed_mesh]") {
    SECTION("fills interior pixels") {
        PixelBuffer buf(30, 30);
        buf.clear(0, 0, 0, 0);

        // Draw a triangle roughly centered: (15,5), (5,25), (25,25)
        buf.fill_triangle_solid(15, 5, 5, 25, 25, 25, 255, 0, 0, 255);

        // Centroid (15, 18) must be filled
        const uint8_t* p = buf.pixel_at(15, 18);
        REQUIRE(p != nullptr);
        CHECK(p[2] == 255); // R
        CHECK(p[1] == 0);   // G
        CHECK(p[0] == 0);   // B
        CHECK(p[3] == 255); // A

        // Bottom edge center (15, 25) should be filled
        CHECK(buf.pixel_at(15, 25)[2] == 255);
    }

    SECTION("does not fill exterior pixels") {
        PixelBuffer buf(30, 30);
        buf.clear(0, 0, 0, 0);

        buf.fill_triangle_solid(15, 5, 5, 25, 25, 25, 255, 0, 0, 255);

        // Well outside the triangle
        CHECK(buf.pixel_at(0, 0)[2] == 0);
        CHECK(buf.pixel_at(29, 0)[2] == 0);
        CHECK(buf.pixel_at(0, 29)[2] == 0);
        CHECK(buf.pixel_at(29, 29)[2] == 0);

        // Just above the apex
        CHECK(buf.pixel_at(15, 3)[2] == 0);
    }

    SECTION("degenerate triangle (collinear points) does not crash") {
        PixelBuffer buf(20, 20);
        buf.clear(0, 0, 0, 0);

        // All on same line -- horizontal
        buf.fill_triangle_solid(0, 10, 10, 10, 20, 10, 255, 0, 0, 255);
        // All on same line -- vertical
        buf.fill_triangle_solid(10, 0, 10, 10, 10, 20, 255, 0, 0, 255);
        // All same point
        buf.fill_triangle_solid(5, 5, 5, 5, 5, 5, 255, 0, 0, 255);
    }

    SECTION("triangle fully off-screen does not crash") {
        PixelBuffer buf(20, 20);
        buf.clear(0, 0, 0, 0);

        // Entirely above
        buf.fill_triangle_solid(5, -30, 0, -20, 10, -20, 255, 0, 0, 255);
        // Entirely below
        buf.fill_triangle_solid(5, 50, 0, 40, 10, 40, 255, 0, 0, 255);
        // Entirely to the right
        buf.fill_triangle_solid(100, 5, 90, 15, 110, 15, 255, 0, 0, 255);

        // Buffer should still be all zeros
        for (int y = 0; y < 20; y++) {
            for (int x = 0; x < 20; x++) {
                CHECK(buf.pixel_at(x, y)[2] == 0);
            }
        }
    }

    SECTION("partially off-screen clips correctly") {
        PixelBuffer buf(20, 20);
        buf.clear(0, 0, 0, 0);

        // Triangle with apex above the buffer
        buf.fill_triangle_solid(10, -10, 0, 15, 19, 15, 255, 0, 0, 255);

        // Some interior pixel near the bottom should be filled
        CHECK(buf.pixel_at(10, 10)[2] == 255);

        // Pixel at top-left corner (0,0) depends on triangle shape --
        // just verify no crash and that clipping worked
        // (the triangle edge at y=0 has limited x range)
    }

    SECTION("vertex order does not matter") {
        PixelBuffer buf1(30, 30);
        PixelBuffer buf2(30, 30);
        buf1.clear(0, 0, 0, 0);
        buf2.clear(0, 0, 0, 0);

        // Same triangle, different vertex order
        buf1.fill_triangle_solid(15, 5, 5, 25, 25, 25, 255, 0, 0, 255);
        buf2.fill_triangle_solid(25, 25, 15, 5, 5, 25, 255, 0, 0, 255);

        // Both should produce identical output
        for (int y = 0; y < 30; y++) {
            for (int x = 0; x < 30; x++) {
                CHECK(buf1.pixel_at(x, y)[2] == buf2.pixel_at(x, y)[2]);
            }
        }
    }
}

// ============================================================================
// fill_triangle_gradient()
// ============================================================================

TEST_CASE("BedMeshBuffer gradient triangle fill", "[bed_mesh]") {
    SECTION("produces color interpolation near vertices") {
        PixelBuffer buf(100, 100);
        buf.clear(0, 0, 0, 0);

        // Red at top, green at bottom-left, blue at bottom-right
        buf.fill_triangle_gradient(50, 5, 255, 0, 0,  // v1: red
                                   5, 90, 0, 255, 0,  // v2: green
                                   95, 90, 0, 0, 255, // v3: blue
                                   255);

        // Near the red vertex (50, 8) -- should be mostly red
        const uint8_t* p_red = buf.pixel_at(50, 8);
        REQUIRE(p_red != nullptr);
        CHECK(p_red[2] > 150); // R channel dominant
        CHECK(p_red[1] < 80);  // G channel low
        CHECK(p_red[0] < 80);  // B channel low

        // Near the green vertex (10, 85) -- should be mostly green
        const uint8_t* p_green = buf.pixel_at(10, 85);
        REQUIRE(p_green != nullptr);
        CHECK(p_green[1] > 150); // G channel dominant
        CHECK(p_green[2] < 100); // R channel lower

        // Near the blue vertex (90, 85) -- should be mostly blue
        const uint8_t* p_blue = buf.pixel_at(90, 85);
        REQUIRE(p_blue != nullptr);
        CHECK(p_blue[0] > 150); // B channel dominant
        CHECK(p_blue[2] < 100); // R channel lower
    }

    SECTION("degenerate case does not crash") {
        PixelBuffer buf(20, 20);
        buf.clear(0, 0, 0, 0);

        // Collinear points
        buf.fill_triangle_gradient(0, 10, 255, 0, 0, 10, 10, 0, 255, 0, 20, 10, 0, 0, 255, 255);
        // Zero-area (all same point)
        buf.fill_triangle_gradient(5, 5, 255, 0, 0, 5, 5, 0, 255, 0, 5, 5, 0, 0, 255, 255);
    }

    SECTION("uniform color produces solid fill") {
        PixelBuffer buf(30, 30);
        buf.clear(0, 0, 0, 0);

        // All vertices same color -- should produce uniform fill
        buf.fill_triangle_gradient(15, 5, 128, 128, 128, 5, 25, 128, 128, 128, 25, 25, 128, 128,
                                   128, 255);

        // Centroid should have the uniform color
        const uint8_t* p = buf.pixel_at(15, 18);
        REQUIRE(p != nullptr);
        CHECK(p[2] == 128); // R
        CHECK(p[1] == 128); // G
        CHECK(p[0] == 128); // B
    }

    SECTION("alpha=0 produces no output") {
        PixelBuffer buf(30, 30);
        buf.clear(0, 0, 0, 0);

        buf.fill_triangle_gradient(15, 5, 255, 0, 0, 5, 25, 0, 255, 0, 25, 25, 0, 0, 255, 0);

        // Buffer should still be all zeros
        for (int y = 0; y < 30; y++) {
            for (int x = 0; x < 30; x++) {
                const uint8_t* p = buf.pixel_at(x, y);
                CHECK(p[2] == 0);
                CHECK(p[1] == 0);
                CHECK(p[0] == 0);
            }
        }
    }
}
