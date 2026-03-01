// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "bed_mesh_renderer.h" // For bed_mesh_renderer_t, bed_mesh_view_state_t

#include <lvgl/lvgl.h>

/**
 * @file bed_mesh_overlays.h
 * @brief Grid lines, axes, and labels for bed mesh visualization
 *
 * Provides overlay rendering functions for the bed mesh 3D view:
 * - Grid lines on mesh surface
 * - Reference grids (Mainsail-style wall grids)
 * - Axis labels (X, Y, Z indicators)
 * - Numeric tick labels showing coordinate values
 *
 * All functions operate on an existing bed_mesh_renderer_t instance and
 * render to an LVGL layer in the helix::mesh namespace.
 */

namespace helix {
namespace mesh {

/**
 * @brief Render grid lines on mesh surface
 *
 * Draws a wireframe grid connecting all mesh probe points using cached
 * screen coordinates. Grid lines help visualize mesh topology and spacing.
 *
 * @param layer LVGL draw layer (from DRAW_POST event callback)
 * @param renderer Renderer instance with valid mesh data and projection cache
 * @param canvas_width Canvas width in pixels
 * @param canvas_height Canvas height in pixels
 */
void render_grid_lines(lv_layer_t* layer, const bed_mesh_renderer_t* renderer, int canvas_width,
                       int canvas_height);

/**
 * @brief Render reference grids (floor and walls)
 *
 * Draws a reference frame around the mesh:
 * - Floor grid (XY plane) below the mesh
 * - Back wall (XZ plane) and left wall (YZ plane)
 *
 * Uses PRINTER BED dimensions (not mesh dimensions) so the mesh "floats" inside.
 * Z range extends 25% above and below mesh to provide visual context.
 * Should be called BEFORE render_mesh_surface() so mesh correctly occludes it.
 *
 * @param layer LVGL draw layer (from DRAW_POST event callback)
 * @param renderer Renderer instance with valid mesh data
 * @param canvas_width Canvas width in pixels
 * @param canvas_height Canvas height in pixels
 */
void render_reference_grids(lv_layer_t* layer, const bed_mesh_renderer_t* renderer,
                            int canvas_width, int canvas_height);

// Legacy stubs - kept for API compatibility
void render_reference_floor(lv_layer_t* layer, const bed_mesh_renderer_t* renderer,
                            int canvas_width, int canvas_height);
void render_reference_walls(lv_layer_t* layer, const bed_mesh_renderer_t* renderer,
                            int canvas_width, int canvas_height);

/**
 * @brief Render axis labels (X, Y, Z indicators)
 *
 * Positions labels at the MIDPOINT of each axis extent, just outside the grid edge:
 * - X label: Middle of X axis extent, below/outside the front edge
 * - Y label: Middle of Y axis extent, to the right/outside the right edge
 * - Z label: At the top of the Z axis, at the back-right corner
 *
 * This matches Mainsail's visualization style where axis labels indicate
 * the direction/dimension rather than the axis endpoint.
 *
 * @param layer LVGL draw layer (from DRAW_POST event callback)
 * @param renderer Renderer instance with valid mesh data
 * @param canvas_width Canvas width in pixels
 * @param canvas_height Canvas height in pixels
 */
void render_axis_labels(lv_layer_t* layer, const bed_mesh_renderer_t* renderer, int canvas_width,
                        int canvas_height);

/**
 * @brief Render numeric tick labels on X, Y, and Z axes
 *
 * Adds millimeter labels (e.g., "-100", "0", "100") at regular intervals along
 * the X and Y axes to show bed dimensions, and height labels on the Z-axis.
 * Uses actual printer coordinates (works with any origin convention).
 *
 * @param layer LVGL draw layer (from DRAW_POST event callback)
 * @param renderer Renderer instance with valid mesh data
 * @param canvas_width Canvas width in pixels
 * @param canvas_height Canvas height in pixels
 */
void render_numeric_axis_ticks(lv_layer_t* layer, const bed_mesh_renderer_t* renderer,
                               int canvas_width, int canvas_height);

/**
 * @brief Draw a single axis tick label at the given screen position
 *
 * Helper function to reduce code duplication in render_numeric_axis_ticks.
 * Handles bounds checking, text formatting, and deferred text copy for LVGL.
 *
 * @param layer LVGL draw layer (from DRAW_POST event callback)
 * @param label_dsc LVGL label drawing descriptor (pre-configured with font, color, opacity)
 * @param screen_x Screen X coordinate for label origin
 * @param screen_y Screen Y coordinate for label origin
 * @param offset_x X offset from screen position (for label alignment)
 * @param offset_y Y offset from screen position (for label alignment)
 * @param value Numeric value to format and display
 * @param canvas_width Canvas width in pixels
 * @param canvas_height Canvas height in pixels
 * @param use_decimals If true, formats with 2 decimal places (for Z-axis mm values)
 *                     If false, formats as whole number (for X/Y axis values)
 */
void draw_axis_tick_label(lv_layer_t* layer, lv_draw_label_dsc_t* label_dsc, int screen_x,
                          int screen_y, int offset_x, int offset_y, double value, int canvas_width,
                          int canvas_height, bool use_decimals = false);

// ========== Buffer-targeted overloads (no LVGL calls) ==========
// These replace lv_draw_line() with PixelBuffer::draw_line().
// Safe to call from background threads.
// Note: axis labels and tick labels are NOT rendered to buffer
// (text rendering requires LVGL font engine).

class PixelBuffer;

/**
 * @brief Render grid lines on mesh surface into a pixel buffer
 *
 * @param buf Target pixel buffer
 * @param renderer Renderer instance with valid mesh data and projection cache
 * @param canvas_width Canvas width in pixels
 * @param canvas_height Canvas height in pixels
 * @param line_r, line_g, line_b Grid line color (pre-fetched from theme)
 */
void render_grid_lines(PixelBuffer& buf, const bed_mesh_renderer_t* renderer, int canvas_width,
                       int canvas_height, uint8_t line_r, uint8_t line_g, uint8_t line_b);

/**
 * @brief Render reference grids (floor and walls) into a pixel buffer
 *
 * @param buf Target pixel buffer
 * @param renderer Renderer instance with valid mesh data
 * @param canvas_width Canvas width in pixels
 * @param canvas_height Canvas height in pixels
 * @param line_r, line_g, line_b Grid line color (pre-fetched from theme)
 */
void render_reference_grids(PixelBuffer& buf, const bed_mesh_renderer_t* renderer, int canvas_width,
                            int canvas_height, uint8_t line_r, uint8_t line_g, uint8_t line_b);

/**
 * @brief Render reference floor into a pixel buffer (delegates to render_reference_grids)
 */
void render_reference_floor(PixelBuffer& buf, const bed_mesh_renderer_t* renderer, int canvas_width,
                            int canvas_height, uint8_t line_r, uint8_t line_g, uint8_t line_b);

/**
 * @brief Render reference walls into a pixel buffer (stub, merged into render_reference_grids)
 */
void render_reference_walls(PixelBuffer& buf, const bed_mesh_renderer_t* renderer, int canvas_width,
                            int canvas_height, uint8_t line_r, uint8_t line_g, uint8_t line_b);

} // namespace mesh
} // namespace helix
