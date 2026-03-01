# Submodule Patches

Local patches applied to git submodules. Applied manually after submodule checkout.

## Base Version

**LVGL**: v9.5.0 (commit `85aa60d18`)

## LVGL Patches

Applied in order. The fbdev patches have a dependency: `stride_bpp` must be applied before `skip_unblank`.

| Patch | File(s) | Purpose |
|-------|---------|---------|
| `lvgl_observer_debug.patch` | `lv_observer.c` | Enhanced error logging with pointer/type info |
| `lvgl_fbdev_stride_bpp.patch` | `lv_linux_fbdev.c` | Fix incorrect bpp on AD5M displays (calculate from stride) |
| `lvgl_fbdev_skip_unblank.patch` | `lv_linux_fbdev.c`, `.h` | Skip FBIOBLANK during splash handoff |
| `lvgl_blend_null_guard.patch` | `lv_draw_sw_blend.c` | Null check before layer/draw_buf deref |
| `lvgl_draw_sw_label_null_guard.patch` | `lv_draw_sw_letter.c` | Null check for font/glyph during draw |
| `lvgl_slider_scroll_chain.patch` | `lv_slider.c` | Fix perpendicular scroll flag logic |
| `lvgl-strdup-null-guard.patch` | `lv_string_builtin.c`, `clib/` | NULL input guard for lv_strdup |
| `lvgl_sdl_window.patch` | `lv_sdl_window.c` | Multi-display positioning, Android support, macOS crash fix |
| `lvgl_theme_breakpoints.patch` | `lv_theme_default.c` | Custom breakpoint tuning for 480-800px |
| `lvgl-drm-flush-rotation.patch` | `lv_linux_drm.c`, `.h` | DRM plane rotation API + 180° software rotation via shadow buffer in flush callback |
| `lvgl-drm-egl-getters.patch` | `lv_linux_drm.h`, `lv_linux_drm_egl.c` | EGL display/context/config getters |
| `lvgl_refr_reshape_null_guard.patch` | `lv_refr.c` | NULL guard on draw_buf reshape failure to prevent buffer overrun (#229) |

## Dropped Patches (v9.5.0)

LVGL 9.5 removed the entire XML system from core. These patches are now in `lib/helix-xml/`:

- `lv_xml.c` / `.h` — `lv_xml_get_const_silent()` addition
- `lv_xml_style.c` — `translate_x`/`translate_y` using `lv_xml_to_size()`
- `lv_xml_image_parser.c` — image "contain"/"cover" alignment enums

## libhv Patches

| Patch | Purpose |
|-------|---------|
| `libhv-dns-resolver-fallback.patch` | DNS resolver fallback for mDNS |
| `libhv-streaming-upload.patch` | Streaming upload support |

## Application

```bash
# Apply all LVGL patches (from project root)
PATCHES="$PWD/patches"
for p in lvgl_observer_debug lvgl_fbdev_stride_bpp lvgl_fbdev_skip_unblank \
         lvgl_blend_null_guard lvgl_draw_sw_label_null_guard \
         lvgl_slider_scroll_chain lvgl-strdup-null-guard \
         lvgl_sdl_window lvgl_theme_breakpoints; do
    git -C lib/lvgl apply "$PATCHES/${p}.patch"
done

# Verify
git -C lib/lvgl diff --stat
# Should show 10 files changed

# Regenerate a patch after manual edits
git -C lib/lvgl diff src/path/to/file.c > patches/patch_name.patch
```

## Backup

`lvgl-local-patches.patch` — combined diff of all LVGL patches (temporary backup, safe to delete).
