# LVGL Gesture Recognition: Pinch-to-Zoom

Research notes for implementing pinch-to-zoom with LVGL 9.5's gesture recognition system on evdev multi-touch hardware.

## Architecture

LVGL's gesture system uses independent recognizers (PINCH, ROTATE, TWO_FINGERS_SWIPE) that all process the same raw touch data. **Only one recognizer can be RECOGNIZED at a time** — when one wins, all others are reset.

### Recognizer Priority (enum order)

```
LV_INDEV_GESTURE_NONE = 0
LV_INDEV_GESTURE_PINCH = 1           ← checked first
LV_INDEV_GESTURE_SWIPE = 2
LV_INDEV_GESTURE_ROTATE = 3          ← checked third
LV_INDEV_GESTURE_TWO_FINGERS_SWIPE = 4
LV_INDEV_GESTURE_SCROLL = 5
```

PINCH (index 1) is iterated **before** ROTATE (index 3) in `lv_indev_gesture_recognizers_update()`. If PINCH reaches RECOGNIZED first, ROTATE is reset. But if PINCH stays in ONGOING and ROTATE reaches RECOGNIZED first, **PINCH gets reset** (scale goes back to 1.0).

## The PINCH vs ROTATE Race

### Default Thresholds

| Recognizer | Threshold | Default | What it means |
|-----------|-----------|---------|---------------|
| PINCH up | `lv_indev_set_pinch_up_threshold()` | **1.5** | Scale must exceed 150% |
| PINCH down | `lv_indev_set_pinch_down_threshold()` | **0.75** | Scale must go below 75% |
| ROTATE | `lv_indev_set_rotation_rad_threshold()` | **0.2 rad** (~11.5°) | Very small angular change |

**Problem**: Any natural two-finger movement has both scale AND rotation components. The rotation threshold (0.2 radians) is trivially easy to hit, while the pinch threshold (1.5x/0.75x) requires significant finger spread. **ROTATE almost always wins the race.**

### What Happens When ROTATE Wins

1. ROTATE reaches RECOGNIZED → all other recognizers reset
2. PINCH's cumulative scale resets to 1.0
3. Next frame: PINCH is back to ONGOING, recomputing from scratch
4. If PINCH reaches RECOGNIZED, its scale starts from 1.0 again
5. Frame-to-frame delta computation sees a huge discontinuity → **visual jump**

## The Solution: Disable Rotation

Since we don't use rotation gestures, **raise the rotation threshold to effectively disable it**:

```cpp
lv_indev_set_pinch_up_threshold(pointer_, 1.05f);    // must be > 1.0f (assertion)
lv_indev_set_pinch_down_threshold(pointer_, 0.95f);   // must be < 1.0f (assertion)
lv_indev_set_rotation_rad_threshold(pointer_, 3.14f);  // ~180°, effectively disabled
```

With rotation disabled, PINCH always wins. No more race condition, no more scale resets.

**Important**: The assertions require `pinch_up > 1.0f` and `pinch_down < 1.0f`. Setting exactly 1.0 will abort in debug builds.

## Official Example Pattern

From `examples/others/gestures/lv_example_gestures.c`:

```c
static void label_scale(lv_event_t * gesture_event)
{
    static float base_scale = 1.0;
    float scale;

    if(lv_event_get_gesture_type(gesture_event) != LV_INDEV_GESTURE_PINCH)
        return;

    lv_indev_gesture_state_t state =
        lv_event_get_gesture_state(gesture_event, LV_INDEV_GESTURE_PINCH);

    // Cumulative: base_scale persists across gestures
    scale = base_scale * lv_event_get_pinch_scale(gesture_event);

    if(state == LV_INDEV_GESTURE_STATE_ENDED) {
        base_scale = scale;  // Save for next gesture
        return;
    }

    if(state == LV_INDEV_GESTURE_STATE_RECOGNIZED) {
        // Apply scale to widget
        if(scale < 0.4f) scale = 0.4f;
        else if(scale > 2.0f) scale = 2.0f;
        // ...
    }
}
```

Key insight: `lv_event_get_pinch_scale()` returns a relative scale factor within the current gesture session (1.0 = no change from start). The official pattern multiplies by a `base_scale` to maintain state across gestures.

## Cumulative Scale Behavior

The scale is cumulative within a gesture:
```c
// gesture_calculate_factors():
g->scale = g->p_scale * sqrtf((a * a) + (b * b));
```

On reset (finger lift, or another recognizer wins):
```c
recognizer->scale = recognizer->info->scale = 1.0;
```

## Gesture State Flow

```
NONE → ONGOING → RECOGNIZED (repeats each frame) → ENDED
                                                  → CANCELED
```

- **ONGOING**: Two fingers detected, recognizer is tracking but threshold not yet met
- **RECOGNIZED**: Threshold exceeded, gesture data available, repeats each frame
- **ENDED**: Fingers lifted cleanly
- **CANCELED**: Finger lifted before threshold was met

## API Reference

```c
// Threshold configuration (call after creating indev)
void lv_indev_set_pinch_up_threshold(lv_indev_t * indev, float threshold);   // > 1.0
void lv_indev_set_pinch_down_threshold(lv_indev_t * indev, float threshold); // < 1.0
void lv_indev_set_rotation_rad_threshold(lv_indev_t * indev, float threshold);

// Event queries (in LV_EVENT_GESTURE callback)
lv_indev_gesture_type_t lv_event_get_gesture_type(lv_event_t * e);
lv_indev_gesture_state_t lv_event_get_gesture_state(lv_event_t * e, lv_indev_gesture_type_t type);
float lv_event_get_pinch_scale(lv_event_t * e);   // cumulative within gesture
float lv_event_get_rotation(lv_event_t * e);       // radians
void lv_indev_get_gesture_center_point(lv_indev_gesture_recognizer_t * r, lv_point_t * point);
```

## Configuration Requirements

```c
// lv_conf.h
#define LV_USE_GESTURE_RECOGNITION 1  // enables multi-touch gesture system
#define LV_USE_FLOAT 1                // REQUIRED for gesture math
```

## Driver Requirements

- **evdev**: Supports multi-touch (processes ABS_MT_SLOT, ABS_MT_POSITION_X/Y, ABS_MT_TRACKING_ID)
- **libinput**: Does NOT support multi-touch in LVGL — explicitly states "We don't support multitouch"
- **wayland**: Supports multi-touch via wl_touch protocol

For DRM backend with touchscreens, **use evdev driver** (`lv_evdev_create`), not libinput.

## Sources

- [LVGL 9.5 Gestures Documentation](https://docs.lvgl.io/master/main-modules/indev/gestures.html)
- [LVGL GitHub Issue #6640 - GESTURE bug](https://github.com/lvgl/lvgl/issues/6640)
- [LVGL Forum - How does LVGL support multi touch](https://forum.lvgl.io/t/how-does-lvgl-support-multi-touch/13078)
- Official example: `examples/others/gestures/lv_example_gestures.c`
- Source: `lib/lvgl/src/indev/lv_indev_gesture.c` (recognizer logic)
- Source: `lib/lvgl/src/drivers/evdev/lv_evdev.c` (MT event processing)
