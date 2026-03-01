<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# FlashForge Adventurer 5X (AD5X) Support

HelixScreen has a dedicated cross-compilation target for the FlashForge Adventurer 5X. The AD5X is a multi-color 3D printer with a 4-channel IFS (Intelligent Filament System). It has its own toolchain and Docker image (`Dockerfile.ad5x`), separate from the Creality K1 series.

**Status: Active testing** — prebuilt binaries are included in releases. See [issue #203](https://github.com/prestonbrown/helixscreen/issues/203).

## Hardware

| Spec | Value |
|------|-------|
| SoC | Ingenic X2600 (XBurst2, MIPS32 R5 compatible) |
| CPU | Dual-core XBurst2 (MIPS) + Victory0 (RISC-V real-time co-processor) |
| Display | 4.3" touch, 800x480, fbdev at `/dev/fb0`, 32bpp BGRA |
| Touch | Resistive, evdev at `/dev/input/eventN` (device name "Touchscreen") |
| RAM | Unconfirmed |
| OS | Custom Linux (BusyBox), not Buildroot |
| Init System | sysvinit (BusyBox), NOT systemd |
| Moonraker | Via ZMOD, port 7125 |
| Multi-Material | IFS (Intelligent Filament System), 4 spools, auto-switching |
| SSH | `root@<ip>` (via ZMOD) |

## Filesystem Layout

The AD5X uses a FlashForge-specific layout, distinct from the Creality K1:

| Path | Purpose |
|------|---------|
| `/usr/data/` | User data partition |
| `/usr/prog/` | FlashForge programs and tools — **key AD5X indicator** |
| `/usr/data/config/` | Klipper/Moonraker config |
| `/usr/data/config/mod/` | ZMOD installation |
| `/usr/data/config/mod_data/` | ZMOD data, logs, database |
| `/opt/config/` | Symlink or bind-mount to `/usr/data/config/` |

The presence of `/usr/prog/` is used for runtime platform detection (K1 vs AD5X).

## Build Target

The AD5X has a dedicated build target and toolchain, separate from the Creality K1:

```bash
make ad5x-docker          # Build AD5X binary (dedicated MIPS32r5 glibc toolchain)
make release-ad5x         # Package as helixscreen-ad5x.zip (AD5X release_info.json)
```

**Toolchain**: Dedicated Docker image (`Dockerfile.ad5x`) with MIPS32r5 glibc cross-compiler, distinct from the K1's musl-based toolchain.

**Platform define**: `-DHELIX_PLATFORM_MIPS`. Runtime detection via `/usr/prog` presence determines platform key (`ad5x` vs `k1`) for update manager asset selection.

## ZMOD Integration

The AD5X runs HelixScreen through the [ZMOD](https://github.com/ghzserg/zmod) firmware modification. ZMOD handles:

- Klipper/Moonraker installation and management
- Display initialization (fbdev, touch via tslib env vars)
- App lifecycle (init.d service script `S80guppyscreen`)
- Update management via Moonraker update manager

### Moonraker Update Manager

ZMOD configures Moonraker to check for HelixScreen updates. The `release_info.json` file tells Moonraker which release asset to download:

```json
{
    "project_name": "helixscreen",
    "project_owner": "prestonbrown",
    "version": "v0.13.1",
    "asset_name": "helixscreen-ad5x.zip"
}
```

### Launch Environment

The ZMOD init script sets up touch input via tslib environment variables, but HelixScreen uses LVGL's built-in evdev driver instead. The relevant environment on the AD5X:

- Touch device: auto-detected from `/dev/input/eventN`
- Framebuffer: `/dev/fb0` (800x480, 32bpp)
- Backlight: `FBIOBLANK` ioctl (standard Linux fbdev)

## Display & Touch

### Display Backend

The AD5X uses the **fbdev** display backend (same as AD5M and K1). No DRM support.

- Resolution: 800x480 (auto-detected from framebuffer)
- Color depth: 32bpp ARGB8888
- Sleep: `FBIOBLANK` / `FB_BLANK_NORMAL` for blanking, `FB_BLANK_UNBLANK` for wake

### Touch Input

HelixScreen uses LVGL's built-in evdev input driver. The ZMOD ecosystem historically used tslib for touch calibration, but our built-in calibration system handles this natively.

If touch input requires calibration (resistive panel with non-linear mapping), the calibration wizard will handle it automatically on first launch.

## IFS (Intelligent Filament System)

The AD5X's 4-channel IFS is its distinguishing feature. **IFS support in HelixScreen is not yet implemented** — this is a future feature request.

IFS capabilities:
- 4 filament spools with auto-switching
- Per-spool color and material tracking
- Filament presence detection
- Purge volume optimization during color changes

The ZMOD ecosystem exposes IFS through Klipper macros and a custom color selection UI. HelixScreen IFS integration would need to interface with these macros via Moonraker's G-code API.

## Differences from AD5M

| Aspect | AD5M | AD5X |
|--------|------|------|
| Architecture | ARM (armv7l, Cortex-A7) | MIPS (Ingenic X2600 XBurst2) |
| Display | 800x480 fbdev | 800x480 fbdev |
| Backlight | `/dev/disp` ioctl (Allwinner sunxi) | `FBIOBLANK` (standard Linux) |
| Config path | `/opt/helixscreen/` | `/usr/data/helixscreen/` |
| Multi-material | No (single extruder) | IFS (4 spools) |
| Build target | `PLATFORM_TARGET=ad5m` | `PLATFORM_TARGET=ad5x` |
| Binary | ARM static (glibc sysroot) | MIPS static (glibc) |

## Known Limitations

- **No IFS UI**: Multi-color filament management not implemented
- **No inotify**: AD5X kernel may lack inotify support (same as AD5M) — XML hot reload may not work
- **No WiFi management**: wpa_supplicant present but may not have usable interfaces
