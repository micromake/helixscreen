#!/bin/bash
# Copyright (C) 2025-2026 356C LLC
# SPDX-License-Identifier: GPL-3.0-or-later
# Take a screenshot from AD5M framebuffer
#
# Usage: ./scripts/ad5m-screenshot.sh [output_name] [hostname]
#        AD5M_HOST=ad5m.local ./scripts/ad5m-screenshot.sh [output_name]
#
# The AD5M framebuffer is 800x480 BGRA format
# Double-buffered, so we only use first half of the data

set -e

OUTPUT="${1:-ad5m_screenshot}"
# Priority: argument > environment variable > default
HOST="${2:-${AD5M_HOST:-ad5m-pc.lan}}"
OUTPUT_PATH="/tmp/${OUTPUT}.png"

echo "Capturing framebuffer from ${HOST}..."
ssh root@${HOST} 'cat /dev/fb0' > /tmp/fb_raw.bin 2>/dev/null

# AD5M fb is 800x480x4 = 1536000 bytes, but double-buffered
echo "Converting to PNG..."
head -c 1536000 /tmp/fb_raw.bin | convert -size 800x480 -depth 8 BGRA:- "${OUTPUT_PATH}"

echo "Screenshot saved to: ${OUTPUT_PATH}"

# Don't auto-open - user can view manually if needed
