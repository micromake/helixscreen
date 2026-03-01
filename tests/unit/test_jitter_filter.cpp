// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025-2026 356C LLC

// Tests for TouchJitterFilter — the jitter filter applied generically in
// lvgl_init.cpp to all backends (DRM, FBDEV, SDL). Tests exercise the
// exact same apply() method used in production, preventing divergence.
//
// Key behavior: the filter suppresses jitter until the first intentional
// movement exceeds the threshold ("breakout"). After breakout, all
// coordinates pass through unfiltered for smooth scrolling/dragging.
//
// The "Goodix scenario" tests validate the core theory: noisy touch
// controllers report coordinate jitter during stationary taps that
// exceeds LVGL's scroll_limit, causing taps to be classified as drags.
// These tests simulate realistic Goodix noise and prove the filter
// prevents scroll detection from triggering.

#include "touch_jitter_filter.h"

#include "../catch_amalgamated.hpp"

TEST_CASE("Jitter filter: disabled when threshold is 0", "[jitter-filter]") {
    TouchJitterFilter f{};

    int32_t x = 100, y = 200;
    f.apply(LV_INDEV_STATE_PRESSED, x, y);
    REQUIRE(x == 100);
    REQUIRE(y == 200);

    x = 103;
    y = 202;
    f.apply(LV_INDEV_STATE_PRESSED, x, y);
    REQUIRE(x == 103);
    REQUIRE(y == 202);
}

TEST_CASE("Jitter filter: first press records position", "[jitter-filter]") {
    TouchJitterFilter f{.threshold_sq = 15 * 15};

    int32_t x = 400, y = 300;
    f.apply(LV_INDEV_STATE_PRESSED, x, y);

    REQUIRE(x == 400);
    REQUIRE(y == 300);
    REQUIRE(f.tracking == true);
    REQUIRE(f.broken_out == false);
    REQUIRE(f.last_x == 400);
    REQUIRE(f.last_y == 300);
}

TEST_CASE("Jitter filter: small movements suppressed before breakout", "[jitter-filter]") {
    TouchJitterFilter f{.threshold_sq = 15 * 15}; // 225

    int32_t x = 400, y = 300;
    f.apply(LV_INDEV_STATE_PRESSED, x, y);

    // Jitter within threshold
    x = 405;
    y = 303;
    f.apply(LV_INDEV_STATE_PRESSED, x, y);
    REQUIRE(x == 400);
    REQUIRE(y == 300);
    REQUIRE(f.broken_out == false);

    // Opposite direction jitter
    x = 395;
    y = 298;
    f.apply(LV_INDEV_STATE_PRESSED, x, y);
    REQUIRE(x == 400);
    REQUIRE(y == 300);

    // Right at boundary: dx=10, dy=10, dist²=200 < 225
    x = 410;
    y = 310;
    f.apply(LV_INDEV_STATE_PRESSED, x, y);
    REQUIRE(x == 400);
    REQUIRE(y == 300);
}

TEST_CASE("Jitter filter: breakout disables filtering for rest of touch", "[jitter-filter]") {
    TouchJitterFilter f{.threshold_sq = 15 * 15};

    int32_t x = 400, y = 300;
    f.apply(LV_INDEV_STATE_PRESSED, x, y);

    // Large movement triggers breakout: dx=20, dist²=400 > 225
    x = 420;
    y = 300;
    f.apply(LV_INDEV_STATE_PRESSED, x, y);
    REQUIRE(x == 420);
    REQUIRE(y == 300);
    REQUIRE(f.broken_out == true);

    // After breakout: small movements pass through unfiltered (smooth scrolling)
    x = 423;
    y = 302;
    f.apply(LV_INDEV_STATE_PRESSED, x, y);
    REQUIRE(x == 423);
    REQUIRE(y == 302);

    // Even 1px movements pass through
    x = 424;
    y = 302;
    f.apply(LV_INDEV_STATE_PRESSED, x, y);
    REQUIRE(x == 424);
    REQUIRE(y == 302);
}

TEST_CASE("Jitter filter: tap release snaps to initial position", "[jitter-filter]") {
    TouchJitterFilter f{.threshold_sq = 15 * 15};

    // Press and jitter without breaking out
    int32_t x = 400, y = 300;
    f.apply(LV_INDEV_STATE_PRESSED, x, y);

    x = 407;
    y = 304;
    f.apply(LV_INDEV_STATE_PRESSED, x, y);
    REQUIRE(x == 400); // Suppressed

    // Release during tap: snaps to initial press position
    x = 408;
    y = 305;
    f.apply(LV_INDEV_STATE_RELEASED, x, y);
    REQUIRE(x == 400);
    REQUIRE(y == 300);
    REQUIRE(f.tracking == false);
    REQUIRE(f.broken_out == false);
}

TEST_CASE("Jitter filter: drag release passes through coordinates", "[jitter-filter]") {
    TouchJitterFilter f{.threshold_sq = 15 * 15};

    // Press and break out (start scrolling)
    int32_t x = 400, y = 300;
    f.apply(LV_INDEV_STATE_PRESSED, x, y);

    x = 420;
    y = 300;
    f.apply(LV_INDEV_STATE_PRESSED, x, y);
    REQUIRE(f.broken_out == true);

    // Continue dragging
    x = 450;
    y = 310;
    f.apply(LV_INDEV_STATE_PRESSED, x, y);
    REQUIRE(x == 450);

    // Release during drag: coordinates pass through (no snap)
    x = 455;
    y = 312;
    f.apply(LV_INDEV_STATE_RELEASED, x, y);
    REQUIRE(x == 455);
    REQUIRE(y == 312);
    REQUIRE(f.tracking == false);
}

TEST_CASE("Jitter filter: reset between taps", "[jitter-filter]") {
    TouchJitterFilter f{.threshold_sq = 15 * 15};

    // First tap (no breakout)
    int32_t x = 100, y = 100;
    f.apply(LV_INDEV_STATE_PRESSED, x, y);
    f.apply(LV_INDEV_STATE_RELEASED, x, y);
    REQUIRE(f.tracking == false);
    REQUIRE(f.broken_out == false);

    // Second tap at different location — fresh start
    x = 500;
    y = 400;
    f.apply(LV_INDEV_STATE_PRESSED, x, y);
    REQUIRE(x == 500);
    REQUIRE(y == 400);
    REQUIRE(f.last_x == 500);
    REQUIRE(f.last_y == 400);
    REQUIRE(f.broken_out == false);
}

TEST_CASE("Jitter filter: breakout resets between touches", "[jitter-filter]") {
    TouchJitterFilter f{.threshold_sq = 10 * 10};

    // First touch: break out (drag)
    int32_t x = 100, y = 100;
    f.apply(LV_INDEV_STATE_PRESSED, x, y);
    x = 120;
    y = 100;
    f.apply(LV_INDEV_STATE_PRESSED, x, y);
    REQUIRE(f.broken_out == true);

    // Release
    f.apply(LV_INDEV_STATE_RELEASED, x, y);

    // Second touch: filter active again (not broken out)
    x = 200;
    y = 200;
    f.apply(LV_INDEV_STATE_PRESSED, x, y);
    REQUIRE(f.broken_out == false);

    // Small jitter suppressed on second touch
    x = 203;
    y = 202;
    f.apply(LV_INDEV_STATE_PRESSED, x, y);
    REQUIRE(x == 200);
    REQUIRE(y == 200);
}

TEST_CASE("Jitter filter: smooth drag after breakout", "[jitter-filter]") {
    TouchJitterFilter f{.threshold_sq = 10 * 10};

    // Start drag
    int32_t x = 100, y = 100;
    f.apply(LV_INDEV_STATE_PRESSED, x, y);

    // Break out: move to (115, 100), dist²=225 > 100
    x = 115;
    y = 100;
    f.apply(LV_INDEV_STATE_PRESSED, x, y);
    REQUIRE(x == 115);
    REQUIRE(f.broken_out == true);

    // All subsequent moves pass through smoothly — no stepping
    x = 118;
    y = 101;
    f.apply(LV_INDEV_STATE_PRESSED, x, y);
    REQUIRE(x == 118);
    REQUIRE(y == 101);

    x = 120;
    y = 102;
    f.apply(LV_INDEV_STATE_PRESSED, x, y);
    REQUIRE(x == 120);
    REQUIRE(y == 102);

    x = 121;
    y = 102;
    f.apply(LV_INDEV_STATE_PRESSED, x, y);
    REQUIRE(x == 121);
    REQUIRE(y == 102);
}

TEST_CASE("Jitter filter: exact threshold boundary", "[jitter-filter]") {
    TouchJitterFilter f{.threshold_sq = 10 * 10}; // 100

    int32_t x = 100, y = 100;
    f.apply(LV_INDEV_STATE_PRESSED, x, y);

    // Exactly at threshold: dx=10, dy=0, dist²=100 == 100 → suppressed (<=)
    x = 110;
    y = 100;
    f.apply(LV_INDEV_STATE_PRESSED, x, y);
    REQUIRE(x == 100);
    REQUIRE(y == 100);
    REQUIRE(f.broken_out == false);

    // One pixel past: dx=11, dy=0, dist²=121 > 100 → breakout
    x = 111;
    y = 100;
    f.apply(LV_INDEV_STATE_PRESSED, x, y);
    REQUIRE(x == 111);
    REQUIRE(y == 100);
    REQUIRE(f.broken_out == true);
}

TEST_CASE("Jitter filter: negative threshold_sq treated as disabled", "[jitter-filter]") {
    TouchJitterFilter f{.threshold_sq = -100};

    int32_t x = 100, y = 200;
    f.apply(LV_INDEV_STATE_PRESSED, x, y);
    REQUIRE(x == 100);
    REQUIRE(y == 200);

    x = 101;
    y = 201;
    f.apply(LV_INDEV_STATE_PRESSED, x, y);
    REQUIRE(x == 101);
    REQUIRE(y == 201);
}

TEST_CASE("Jitter filter: release without prior press is no-op", "[jitter-filter]") {
    TouchJitterFilter f{.threshold_sq = 15 * 15};

    int32_t x = 300, y = 400;
    f.apply(LV_INDEV_STATE_RELEASED, x, y);
    REQUIRE(x == 300);
    REQUIRE(y == 400);
    REQUIRE(f.tracking == false);
}

// ---------------------------------------------------------------------------
// Goodix scenario tests — validate the core theory
//
// LVGL scroll detection works by accumulating coordinate deltas into
// scroll_sum.  When |scroll_sum.x| or |scroll_sum.y| exceeds scroll_limit
// (default 10px), the touch transitions from "click candidate" to "scroll"
// and click events are never fired.
//
// Goodix GT9xx controllers report noisy coordinates during stationary taps,
// easily producing ±5-12px of jitter.  Without filtering, this noise
// accumulates in scroll_sum and exceeds scroll_limit, making it impossible
// to click anything.
//
// These tests replay realistic noise sequences through the actual
// TouchJitterFilter::apply() and verify that LVGL's scroll detection
// would NOT trigger.
// ---------------------------------------------------------------------------

namespace {

/// Simulate LVGL's scroll_sum accumulation logic.
/// LVGL computes vect = current_pos - prev_pos each tick, then accumulates
/// scroll_sum += vect.  Returns max(|scroll_sum.x|, |scroll_sum.y|).
struct ScrollSimulator {
    int32_t prev_x = 0;
    int32_t prev_y = 0;
    int32_t scroll_sum_x = 0;
    int32_t scroll_sum_y = 0;
    bool started = false;

    /// Feed a filtered coordinate and accumulate the delta
    void feed(int32_t x, int32_t y) {
        if (started) {
            scroll_sum_x += (x - prev_x);
            scroll_sum_y += (y - prev_y);
        }
        prev_x = x;
        prev_y = y;
        started = true;
    }

    int32_t max_scroll_sum() const {
        return std::max(std::abs(scroll_sum_x), std::abs(scroll_sum_y));
    }
};

} // namespace

TEST_CASE("Goodix scenario: unfiltered tap noise exceeds scroll_limit", "[jitter-filter][goodix]") {
    // Prove the problem: WITHOUT jitter filter, realistic Goodix noise during
    // a stationary tap causes scroll_sum to exceed scroll_limit.
    //
    // Goodix GT9xx noise jitters ±5-12px around the true position.  While
    // the absolute position stays near the anchor, the tick-to-tick deltas
    // (what LVGL calls "vect") can be large.  When the noise has any bias
    // (common in capacitive controllers), scroll_sum accumulates past
    // scroll_limit even though the finger never moves.
    struct Sample {
        int32_t x, y;
    };
    // Realistic Goodix noise: finger at ~(400,300), oscillating with a
    // slight Y-axis bias (each cycle drifts ~2px downward).
    // All coordinates stay within ±12px of center — well within the
    // 15px jitter threshold — but the cumulative Y scroll_sum exceeds 10.
    const Sample noise[] = {
        {400, 300}, {405, 303}, {396, 306}, {404, 309}, {397, 312},
        {406, 308}, {394, 311}, {403, 307}, {398, 313}, {405, 310},
    };

    constexpr int scroll_limit = 10; // LVGL default

    ScrollSimulator sim;
    for (const auto& s : noise) {
        sim.feed(s.x, s.y);
    }

    INFO("Unfiltered scroll_sum: x=" << sim.scroll_sum_x << " y=" << sim.scroll_sum_y);
    // Without filtering, the biased Y noise accumulates past scroll_limit.
    // LVGL triggers scroll when |scroll_sum| >= scroll_limit (see
    // lv_indev_scroll.c line 392-414).
    REQUIRE(sim.max_scroll_sum() >= scroll_limit);
}

TEST_CASE("Goodix scenario: filtered tap noise stays below scroll_limit",
          "[jitter-filter][goodix]") {
    // Prove the fix: WITH jitter filter (15px dead zone), the same Goodix
    // noise sequence results in zero scroll_sum because all coordinates
    // are snapped to the initial press position.
    struct Sample {
        int32_t x, y;
    };
    // Same noise sequence as above
    const Sample noise[] = {
        {400, 300}, {405, 303}, {396, 306}, {404, 309}, {397, 312},
        {406, 308}, {394, 311}, {403, 307}, {398, 313}, {405, 310},
    };

    TouchJitterFilter f{.threshold_sq = 15 * 15}; // Production default
    ScrollSimulator sim;

    for (const auto& s : noise) {
        int32_t x = s.x, y = s.y;
        f.apply(LV_INDEV_STATE_PRESSED, x, y);
        sim.feed(x, y);
    }

    INFO("Filtered max scroll_sum: " << sim.max_scroll_sum());
    // With filtering, scroll_sum stays at ZERO — all coords snapped to anchor
    REQUIRE(sim.max_scroll_sum() == 0);
    REQUIRE_FALSE(f.broken_out);

    // Release also snaps to anchor
    int32_t rx = 402, ry = 301;
    f.apply(LV_INDEV_STATE_RELEASED, rx, ry);
    REQUIRE(rx == 400);
    REQUIRE(ry == 300);
}

TEST_CASE("Goodix scenario: intentional drag breaks through filter", "[jitter-filter][goodix]") {
    // The filter must NOT prevent real drags/scrolls.  When the user
    // intentionally moves their finger beyond the dead zone, coordinates
    // should pass through and scrolling should work normally.
    TouchJitterFilter f{.threshold_sq = 15 * 15};
    ScrollSimulator sim;

    constexpr int scroll_limit = 10; // LVGL default

    // Initial press
    int32_t x = 400, y = 300;
    f.apply(LV_INDEV_STATE_PRESSED, x, y);
    sim.feed(x, y);

    // Small jitter (suppressed)
    x = 405;
    y = 302;
    f.apply(LV_INDEV_STATE_PRESSED, x, y);
    sim.feed(x, y);
    REQUIRE(sim.max_scroll_sum() == 0);

    // Intentional drag: finger moves 25px down (well past 15px threshold)
    x = 400;
    y = 325;
    f.apply(LV_INDEV_STATE_PRESSED, x, y);
    sim.feed(x, y);
    REQUIRE(f.broken_out);

    // Continue dragging
    x = 400;
    y = 350;
    f.apply(LV_INDEV_STATE_PRESSED, x, y);
    sim.feed(x, y);

    INFO("Drag scroll_sum: " << sim.max_scroll_sum());
    // scroll_sum should now exceed scroll_limit — scroll detection triggers
    REQUIRE(sim.max_scroll_sum() > scroll_limit);
}

TEST_CASE("Goodix scenario: rapid noisy taps produce clean clicks", "[jitter-filter][goodix]") {
    // Simulate multiple rapid taps with noise — each should produce clean
    // coordinates with zero accumulated scroll.
    TouchJitterFilter f{.threshold_sq = 15 * 15};

    struct Tap {
        int32_t press_x, press_y;
        // Noise offsets applied during the tap
        int32_t noise_dx[3], noise_dy[3];
    };

    const Tap taps[] = {
        {100, 200, {4, -3, 7}, {-2, 5, -4}},
        {300, 150, {-6, 8, -3}, {3, -5, 7}},
        {500, 400, {9, -7, 5}, {-6, 4, -8}},
    };

    for (const auto& tap : taps) {
        ScrollSimulator sim;

        // Press
        int32_t x = tap.press_x, y = tap.press_y;
        f.apply(LV_INDEV_STATE_PRESSED, x, y);
        sim.feed(x, y);

        // Noisy samples during press
        for (int i = 0; i < 3; i++) {
            x = tap.press_x + tap.noise_dx[i];
            y = tap.press_y + tap.noise_dy[i];
            f.apply(LV_INDEV_STATE_PRESSED, x, y);
            sim.feed(x, y);
        }

        REQUIRE(sim.max_scroll_sum() == 0);
        REQUIRE_FALSE(f.broken_out);

        // Release
        x = tap.press_x + 2;
        y = tap.press_y - 1;
        f.apply(LV_INDEV_STATE_RELEASED, x, y);

        // Release coordinates snapped to original press
        REQUIRE(x == tap.press_x);
        REQUIRE(y == tap.press_y);
    }
}
