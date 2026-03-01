// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <lvgl/lvgl.h>

#include <cstdlib>
#include <string>
#include <vector>

namespace helix {
namespace gcode {

/// Shared per-tool color palette used by both 2D and 3D G-code renderers.
/// Converts hex color strings from gcode metadata into lv_color_t values
/// and resolves per-segment colors with optional external override support.
struct GCodeColorPalette {
    std::vector<lv_color_t> tool_colors; ///< From gcode metadata (one per tool)
    lv_color_t override_color{};         ///< External override (AMS/Spoolman)
    bool has_override = false;

    /// Resolve color for a given tool index.
    /// Priority: per-tool color > single override > fallback.
    /// When set_tool_color_overrides() populates tool_colors with AMS slot colors,
    /// those take precedence. Single override is for legacy single-tool path.
    lv_color_t resolve(int tool_index, lv_color_t fallback) const {
        if (tool_index >= 0 && tool_index < static_cast<int>(tool_colors.size())) {
            return tool_colors[static_cast<size_t>(tool_index)];
        }
        if (has_override) {
            return override_color;
        }
        return fallback;
    }

    /// Populate tool_colors from hex strings (e.g., "#ED1C24")
    void set_from_hex_palette(const std::vector<std::string>& hex_colors) {
        tool_colors.clear();
        tool_colors.reserve(hex_colors.size());
        for (const auto& hex : hex_colors) {
            if (hex.size() >= 2 && hex[0] == '#') {
                auto val = static_cast<uint32_t>(std::strtol(hex.c_str() + 1, nullptr, 16));
                tool_colors.push_back(lv_color_hex(val));
            }
        }
    }

    /// Check if palette has any tool colors
    bool has_tool_colors() const {
        return !tool_colors.empty();
    }
};

} // namespace gcode
} // namespace helix
