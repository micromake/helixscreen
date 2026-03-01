// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file drm_rotation_strategy.h
 * @brief DRM plane rotation decision logic (hardware vs software fallback)
 *
 * Pure logic, no DRM dependencies — can be tested without hardware.
 * Used by DisplayBackendDRM::set_display_rotation() to decide whether
 * to use DRM plane rotation or LVGL matrix rotation.
 */

#pragma once

#include <cstdint>

/**
 * @brief Strategy for applying display rotation on DRM backend
 */
enum class DrmRotationStrategy {
    NONE,     ///< No rotation needed (0°)
    HARDWARE, ///< Use DRM plane rotation property
    SOFTWARE  ///< Use LVGL matrix rotation (lv_display_set_matrix_rotation)
};

/**
 * @brief Decide how to rotate the display on a DRM backend
 *
 * Examines the requested rotation against the DRM plane's supported
 * rotation bitmask to choose the best strategy:
 * - 0° always returns NONE (no rotation needed)
 * - If the plane supports the requested angle, returns HARDWARE
 * - Otherwise returns SOFTWARE (LVGL matrix rotation fallback)
 *
 * @param requested_drm_rot  DRM_MODE_ROTATE_* constant for the desired angle
 * @param supported_mask     Bitmask of supported rotations from the plane property
 *                           (0 = no rotation property exists)
 * @return Strategy to use for this rotation
 */
DrmRotationStrategy choose_drm_rotation_strategy(uint64_t requested_drm_rot,
                                                 uint64_t supported_mask);
