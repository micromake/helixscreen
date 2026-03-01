// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

/**
 * @brief Manages font and image registration with LVGL XML system
 *
 * Provides static methods for registering fonts and images that can be
 * referenced by name in XML layout files. Extracted from main.cpp
 * register_fonts_and_images() to enable isolated testing.
 *
 * All methods are static since assets are registered globally with LVGL.
 * Registration is idempotent - calling multiple times is safe.
 *
 * Font registration is breakpoint-aware: fonts only used at larger breakpoints
 * are skipped on smaller screens, saving ~500-800KB of .rodata pages.
 *
 * @code
 * // Register all assets at startup
 * AssetManager::register_all();
 *
 * // Or register separately
 * AssetManager::register_fonts();
 * AssetManager::register_images();
 * @endcode
 */
class AssetManager {
  public:
    /**
     * @brief Register fonts with LVGL XML system, skipping unused sizes
     *
     * Uses the current LVGL display's vertical resolution to determine
     * the active breakpoint and skip fonts that are only used at larger
     * breakpoints. Falls back to registering all fonts if no display exists.
     *
     * Registers:
     * - MDI icon fonts (16, 24, 32, 48, 64px) — all breakpoints
     * - Noto Sans regular fonts (10-28px) — subset by breakpoint
     * - Noto Sans bold fonts (14-28px) — all breakpoints (used in watchdog/modals)
     * - Noto Sans light fonts (10-18px) — subset by breakpoint
     * - Montserrat aliases (for XML compatibility) — subset by breakpoint
     */
    static void register_fonts();

    /**
     * @brief Register all images with LVGL XML system
     *
     * Registers common images used in XML layouts:
     * - Printer placeholder images
     * - Filament spool graphics
     * - Thumbnail placeholders
     * - SVG icons
     */
    static void register_images();

    /**
     * @brief Register all assets (fonts and images)
     *
     * Convenience method that calls register_fonts() and register_images().
     */
    static void register_all();

    /**
     * @brief Check if fonts have been registered
     * @return true if register_fonts() has been called
     */
    static bool fonts_registered();

    /**
     * @brief Check if images have been registered
     * @return true if register_images() has been called
     */
    static bool images_registered();

  private:
    static bool s_fonts_registered;
    static bool s_images_registered;
};
