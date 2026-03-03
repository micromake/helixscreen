// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/**
 * @file screenshot.h
 * @brief Screenshot capture utilities
 *
 * Provides BMP screenshot capture functionality using LVGL's snapshot API.
 */

#include <cstdint>
#include <string>

namespace helix {

/**
 * @brief Write raw ARGB8888 pixel data to a BMP file
 * @param filename Output file path
 * @param data Pixel data (ARGB8888 format)
 * @param width Image width in pixels
 * @param height Image height in pixels
 * @return true on success, false on failure
 */
bool write_bmp(const char* filename, const uint8_t* data, int width, int height);

/**
 * @brief Take a screenshot of the active LVGL screen and save to /tmp
 *
 * Generates a unique filename with timestamp: /tmp/ui-screenshot-<timestamp>.bmp
 * @return The filename on success, empty string on failure
 */
std::string save_screenshot();

} // namespace helix
