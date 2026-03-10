# Lock Screen Design

## Overview

A numeric PIN lock screen for shared-space security. When locked, a full-screen overlay absorbs all input except E-Stop. Users unlock by entering a 4–6 digit PIN on a numeric keypad. Lock engages manually via a home panel widget or automatically after the idle/screensaver timeout.

## Lock State Machine

```
UNLOCKED ──(manual lock widget tap)──► LOCKED
UNLOCKED ──(idle timeout + PIN set)──► LOCKED
LOCKED   ──(correct PIN entered)────► UNLOCKED
LOCKED   ──(E-Stop tap)─────────────► stays LOCKED (E-Stop fires, screen stays locked)
```

## PIN Configuration

- New **"Security"** section in Settings panel (between Display and System)
- **Set PIN**: enter new PIN twice to confirm (4–6 digits)
- **Change PIN**: enter current PIN, then new PIN twice
- **Remove PIN**: enter current PIN to disable
- PIN stored as SHA-256 hash in `settings.json` under `security.pin_hash`
- Auto-lock toggle: on/off (only visible when PIN is set) — ties into existing screensaver/sleep timeout
- No PIN set = lock widget hidden from catalog, auto-lock disabled

## Lock Screen UI

- Full-screen overlay on `lv_layer_top()` (same layer as screensaver)
- Absorbs all touch events except E-Stop button region
- Shows:
  - Lock icon + "Enter PIN" text
  - 4–6 dot indicators (filled as digits entered)
  - Numeric keypad (0–9, backspace, confirm)
  - Printer name or status line at top (view-only, no interaction)
- Wrong PIN: dots shake animation, brief delay before retry (500ms)
- No attempt limit (makerspace users forget PINs — lockout would require reflashing)

## Home Panel Widget

- **Widget ID**: `lock`
- **Display name**: "Lock Screen"
- **Icon**: `lock`
- **Size**: 1x1, not resizable
- **Behavior**: tap locks immediately (no confirmation)
- **Visibility**: hidden from widget catalog when no PIN is set
- **Hardware gate**: none (gated by PIN existence instead)

## Auto-Lock Integration

- When auto-lock is enabled and a PIN is set, the existing screensaver wake flow changes:
  - Currently: touch → screensaver dismissed → UI accessible
  - With auto-lock: touch → screensaver dismissed → lock screen shown → PIN required
- Implementation: `DisplayManager::wake_display()` checks lock state and shows lock overlay instead of returning to normal UI

## E-Stop Bypass

- E-Stop button lives in the top bar, rendered on `lv_layer_sys()` (above `lv_layer_top()`)
- Lock overlay is on `lv_layer_top()`, so E-Stop remains clickable above it
- If E-Stop is on the same layer, raise it above the lock overlay explicitly

## What's NOT Included (YAGNI)

- Pattern unlock (future enhancement)
- Multiple user PINs / user accounts
- Per-action PIN prompts (tiered locking)
- Remote lock/unlock via API
- PIN recovery mechanism (user can delete `security.pin_hash` from settings.json via SSH)
