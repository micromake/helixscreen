// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/**
 * @file bed_mesh_rasterizer.h
 * @brief Triangle rasterization for bed mesh visualization
 *
 * Provides scanline-based triangle fill algorithms with support for:
 * - Solid color fills
 * - Per-vertex gradient interpolation
 * - Adaptive segment counts for performance optimization
 *
 * Uses LVGL's draw layer API for hardware-accelerated rendering.
 */

#include <lvgl/lvgl.h>

namespace helix {
namespace mesh {

/// Default opacity for mesh triangle surfaces (100% - fully opaque)
constexpr lv_opa_t MESH_TRIANGLE_OPACITY = LV_OPA_COVER;

/// Minimum line width for gradient rasterization (use solid color below this)
constexpr int GRADIENT_MIN_LINE_WIDTH = 3;

// ========== Adaptive Gradient Rasterization Constants ==========
// Line width thresholds for adaptive segment count
constexpr int GRADIENT_THIN_LINE_THRESHOLD = 20;   // Lines < 20px use 2 segments
constexpr int GRADIENT_MEDIUM_LINE_THRESHOLD = 50; // Lines 20-49px use 3 segments

// Segment counts for different line widths
constexpr int GRADIENT_THIN_SEGMENT_COUNT = 2;   // Thin lines: 2 segments (faster)
constexpr int GRADIENT_MEDIUM_SEGMENT_COUNT = 3; // Medium lines: 3 segments (balanced)
constexpr int GRADIENT_WIDE_SEGMENT_COUNT = 4;   // Wide lines: 4 segments (better quality)

// Gradient sampling position within segment (0.0 = start, 0.5 = center, 1.0 = end)
constexpr double GRADIENT_SEGMENT_SAMPLE_POSITION = 0.5;

/**
 * @brief Fill triangle with solid color using scanline rasterization
 *
 * Uses batched rectangle draws for performance (15-20% faster than pixel-by-pixel).
 * LVGL's layer system handles clipping automatically.
 *
 * @param layer LVGL draw layer
 * @param x1, y1 First vertex coordinates
 * @param x2, y2 Second vertex coordinates
 * @param x3, y3 Third vertex coordinates
 * @param color Fill color
 * @param opacity Fill opacity (default: MESH_TRIANGLE_OPACITY)
 */
void fill_triangle_solid(lv_layer_t* layer, int x1, int y1, int x2, int y2, int x3, int y3,
                         lv_color_t color, lv_opa_t opacity = MESH_TRIANGLE_OPACITY);

/**
 * @brief Fill triangle with per-vertex color gradient
 *
 * Uses adaptive segment count based on line width for optimal performance/quality:
 * - Thin lines (< 20px): 2 segments
 * - Medium lines (20-49px): 3 segments
 * - Wide lines (50+ px): 4 segments
 *
 * This reduces draw calls by 50-66% compared to fixed segment counts.
 *
 * @param layer LVGL draw layer
 * @param x1, y1, c1 First vertex position and color
 * @param x2, y2, c2 Second vertex position and color
 * @param x3, y3, c3 Third vertex position and color
 * @param opacity Fill opacity (default: MESH_TRIANGLE_OPACITY)
 */
void fill_triangle_gradient(lv_layer_t* layer, int x1, int y1, lv_color_t c1, int x2, int y2,
                            lv_color_t c2, int x3, int y3, lv_color_t c3,
                            lv_opa_t opacity = MESH_TRIANGLE_OPACITY);

// ========== Buffer-targeted overloads (no LVGL calls) ==========
// These extract r/g/b from lv_color_t and delegate to PixelBuffer methods.
// Safe to call from background threads.

class PixelBuffer;

/**
 * @brief Fill triangle with solid color into a pixel buffer
 *
 * @param buf Target pixel buffer
 * @param x1, y1 First vertex coordinates
 * @param x2, y2 Second vertex coordinates
 * @param x3, y3 Third vertex coordinates
 * @param color Fill color (r/g/b extracted from lv_color_t)
 * @param opacity Fill opacity (default: MESH_TRIANGLE_OPACITY)
 */
void fill_triangle_solid(PixelBuffer& buf, int x1, int y1, int x2, int y2, int x3, int y3,
                         lv_color_t color, lv_opa_t opacity = MESH_TRIANGLE_OPACITY);

/**
 * @brief Fill triangle with per-vertex color gradient into a pixel buffer
 *
 * @param buf Target pixel buffer
 * @param x1, y1, c1 First vertex position and color
 * @param x2, y2, c2 Second vertex position and color
 * @param x3, y3, c3 Third vertex position and color
 * @param opacity Fill opacity (default: MESH_TRIANGLE_OPACITY)
 */
void fill_triangle_gradient(PixelBuffer& buf, int x1, int y1, lv_color_t c1, int x2, int y2,
                            lv_color_t c2, int x3, int y3, lv_color_t c3,
                            lv_opa_t opacity = MESH_TRIANGLE_OPACITY);

} // namespace mesh
} // namespace helix
