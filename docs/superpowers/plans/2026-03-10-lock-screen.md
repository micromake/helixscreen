# Lock Screen Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a PIN-protected lock screen for shared-space security (makerspaces, schools, offices).

**Architecture:** A `LockManager` singleton manages lock state and PIN verification. The lock screen is a full-screen overlay on `lv_layer_top()` with a numeric keypad. A panel widget provides manual locking. Auto-lock hooks into `DisplayManager::wake_display()`. E-Stop is duplicated on the lock overlay for safety bypass.

**Tech Stack:** C++, LVGL 9.5, XML declarative UI, SHA-256 (PicoSHA2 header-only library)

**Spec:** `docs/superpowers/specs/2026-03-10-lock-screen-design.md`

---

## File Structure

| File | Responsibility |
|------|---------------|
| `include/lock_manager.h` | Lock state, PIN hash storage/verification, auto-lock config |
| `src/system/lock_manager.cpp` | LockManager implementation |
| `include/ui_lock_screen.h` | Lock screen overlay class |
| `src/ui/ui_lock_screen.cpp` | Lock screen overlay — keypad, PIN entry, shake animation |
| `ui_xml/components/lock_screen.xml` | Lock screen overlay layout |
| `ui_xml/components/panel_widget_lock.xml` | Lock panel widget (1x1) |
| `src/ui/panel_widgets/lock_widget.cpp` | Lock widget registration and tap handler |
| `src/ui/panel_widgets/lock_widget.h` | Lock widget header |
| `ui_xml/components/pin_entry_modal.xml` | Reusable PIN entry modal (heading + keypad + dots) |
| `src/ui/ui_pin_entry_modal.cpp` | PIN entry modal — callback-based API for set/change/remove flows |
| `include/ui_pin_entry_modal.h` | PIN entry modal header |
| `ui_xml/security_settings_overlay.xml` | Security settings section layout |
| `src/ui/ui_settings_security.cpp` | Security settings overlay — set/change/remove PIN, auto-lock toggle |
| `include/ui_settings_security.h` | Security settings header |
| `lib/picosha2.h` | PicoSHA2 header-only SHA-256 (MIT license, public domain) |
| `tests/unit/test_lock_manager.cpp` | Unit tests for LockManager |

---

## Chunk 1: Core Lock Manager

### Task 1: Add PicoSHA2 dependency

**Files:**
- Create: `lib/picosha2.h`

- [ ] **Step 1: Download PicoSHA2 header**

Download from https://github.com/okdshin/PicoSHA2 — single header file, MIT license. Place at `lib/picosha2.h`.

- [ ] **Step 2: Commit**

```bash
git add lib/picosha2.h
git commit -m "chore(deps): add PicoSHA2 header-only SHA-256 library"
```

### Task 2: Write LockManager tests

**Files:**
- Create: `tests/unit/test_lock_manager.cpp`

- [ ] **Step 1: Write failing tests**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#include "lock_manager.h"

// [L065] Use friend class pattern instead of public test-only methods
class LockManagerTestAccess {
public:
    static void reset(helix::LockManager& mgr) {
        mgr.pin_hash_.clear();
        mgr.locked_ = false;
        mgr.auto_lock_ = false;
    }
};

TEST_CASE("LockManager: no PIN set by default", "[lock]") {
    auto& mgr = helix::LockManager::instance();
    LockManagerTestAccess::reset(mgr);

    CHECK_FALSE(mgr.has_pin());
    CHECK_FALSE(mgr.is_locked());
}

TEST_CASE("LockManager: set and verify PIN", "[lock]") {
    auto& mgr = helix::LockManager::instance();
    LockManagerTestAccess::reset(mgr);

    mgr.set_pin("1234");
    CHECK(mgr.has_pin());
    CHECK(mgr.verify_pin("1234"));
    CHECK_FALSE(mgr.verify_pin("0000"));
    CHECK_FALSE(mgr.verify_pin("12345"));
    CHECK_FALSE(mgr.verify_pin(""));
}

TEST_CASE("LockManager: lock and unlock", "[lock]") {
    auto& mgr = helix::LockManager::instance();
    LockManagerTestAccess::reset(mgr);
    mgr.set_pin("5678");

    CHECK_FALSE(mgr.is_locked());

    mgr.lock();
    CHECK(mgr.is_locked());

    CHECK_FALSE(mgr.try_unlock("0000"));
    CHECK(mgr.is_locked());

    CHECK(mgr.try_unlock("5678"));
    CHECK_FALSE(mgr.is_locked());
}

TEST_CASE("LockManager: lock without PIN does nothing", "[lock]") {
    auto& mgr = helix::LockManager::instance();
    LockManagerTestAccess::reset(mgr);

    mgr.lock();
    CHECK_FALSE(mgr.is_locked());
}

TEST_CASE("LockManager: remove PIN unlocks", "[lock]") {
    auto& mgr = helix::LockManager::instance();
    LockManagerTestAccess::reset(mgr);
    mgr.set_pin("1234");
    mgr.lock();
    CHECK(mgr.is_locked());

    mgr.remove_pin();
    CHECK_FALSE(mgr.has_pin());
    CHECK_FALSE(mgr.is_locked());
}

TEST_CASE("LockManager: PIN validation rejects invalid lengths", "[lock]") {
    auto& mgr = helix::LockManager::instance();
    LockManagerTestAccess::reset(mgr);

    CHECK_FALSE(mgr.set_pin("123"));    // too short
    CHECK_FALSE(mgr.set_pin("1234567")); // too long
    CHECK_FALSE(mgr.has_pin());

    CHECK(mgr.set_pin("1234"));   // 4 digits OK
    mgr.remove_pin();
    CHECK(mgr.set_pin("123456")); // 6 digits OK
}

TEST_CASE("LockManager: auto-lock setting", "[lock]") {
    auto& mgr = helix::LockManager::instance();
    LockManagerTestAccess::reset(mgr);

    CHECK_FALSE(mgr.auto_lock_enabled());

    mgr.set_auto_lock(true);
    CHECK(mgr.auto_lock_enabled());

    mgr.set_auto_lock(false);
    CHECK_FALSE(mgr.auto_lock_enabled());
}

TEST_CASE("LockManager: lock is idempotent", "[lock]") {
    auto& mgr = helix::LockManager::instance();
    LockManagerTestAccess::reset(mgr);
    mgr.set_pin("1234");

    mgr.lock();
    mgr.lock();  // second call should be harmless
    CHECK(mgr.is_locked());

    CHECK(mgr.try_unlock("1234"));
    CHECK_FALSE(mgr.is_locked());
}

TEST_CASE("LockManager: set_pin while locked updates PIN", "[lock]") {
    auto& mgr = helix::LockManager::instance();
    LockManagerTestAccess::reset(mgr);
    mgr.set_pin("1234");
    mgr.lock();

    CHECK(mgr.set_pin("5678"));  // change PIN while locked
    CHECK(mgr.is_locked());      // stays locked
    CHECK_FALSE(mgr.verify_pin("1234"));
    CHECK(mgr.verify_pin("5678"));
    CHECK(mgr.try_unlock("5678"));
}
```

- [ ] **Step 2: Run tests — verify they fail**

```bash
make test && ./build/bin/helix-tests "[lock]" -v
```

Expected: compilation error — `lock_manager.h` does not exist.

- [ ] **Step 3: Commit failing tests**

```bash
git add tests/unit/test_lock_manager.cpp
git commit -m "test(lock): add LockManager unit tests"
```

### Task 3: Implement LockManager

**Files:**
- Create: `include/lock_manager.h`
- Create: `src/system/lock_manager.cpp`

- [ ] **Step 1: Write LockManager header**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

namespace helix {

class LockManager {
public:
    static LockManager& instance();

    /// PIN management
    bool has_pin() const;
    bool set_pin(const std::string& pin);    // returns false if invalid length
    void remove_pin();
    bool verify_pin(const std::string& pin) const;

    /// Lock state
    bool is_locked() const;
    void lock();                              // no-op if no PIN set
    bool try_unlock(const std::string& pin);  // returns true if correct

    /// Auto-lock
    bool auto_lock_enabled() const;
    void set_auto_lock(bool enabled);

    /// LVGL subjects (call after LVGL init)
    void init_subjects();

private:
    LockManager();
    friend class LockManagerTestAccess;

    std::string hash_pin(const std::string& pin) const;
    void load_from_config();
    void save_to_config();

    std::string pin_hash_;
    bool locked_ = false;
    bool auto_lock_ = false;
    bool subjects_initialized_ = false;
    lv_subject_t pin_set_subject_{};  // int: 1 when PIN exists, 0 when not

    static constexpr int kMinPinLength = 4;
    static constexpr int kMaxPinLength = 6;
};

} // namespace helix
```

- [ ] **Step 2: Write LockManager implementation**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "lock_manager.h"
#include "config.h"
#include "picosha2.h"

#include <spdlog/spdlog.h>

namespace helix {

LockManager& LockManager::instance() {
    static LockManager inst;
    return inst;
}

LockManager::LockManager() {
    load_from_config();
}

bool LockManager::has_pin() const {
    return !pin_hash_.empty();
}

bool LockManager::set_pin(const std::string& pin) {
    if (pin.length() < kMinPinLength || pin.length() > kMaxPinLength) {
        return false;
    }
    pin_hash_ = hash_pin(pin);
    save_to_config();
    spdlog::info("[LockManager] PIN set");
    return true;
}

void LockManager::remove_pin() {
    pin_hash_.clear();
    locked_ = false;
    save_to_config();
    spdlog::info("[LockManager] PIN removed, lock disabled");
}

bool LockManager::verify_pin(const std::string& pin) const {
    if (pin_hash_.empty() || pin.empty()) return false;
    return hash_pin(pin) == pin_hash_;
}

bool LockManager::is_locked() const {
    return locked_;
}

void LockManager::lock() {
    if (!has_pin()) return;
    locked_ = true;
    spdlog::info("[LockManager] Screen locked");
}

bool LockManager::try_unlock(const std::string& pin) {
    if (verify_pin(pin)) {
        locked_ = false;
        spdlog::info("[LockManager] Screen unlocked");
        return true;
    }
    spdlog::debug("[LockManager] Unlock failed — wrong PIN");
    return false;
}

bool LockManager::auto_lock_enabled() const {
    return auto_lock_;
}

void LockManager::set_auto_lock(bool enabled) {
    auto_lock_ = enabled;
    save_to_config();
}

// SHA-256 of a 4-6 digit PIN is trivially brute-forceable (~1.1M possibilities).
// Acceptable for the "prevent casual makerspace use" threat model.
std::string LockManager::hash_pin(const std::string& pin) const {
    return picosha2::hash256_hex_string(pin);
}

void LockManager::load_from_config() {
    auto* config = Config::get_instance();
    if (!config) return;
    pin_hash_ = config->get<std::string>("/security/pin_hash", "");
    auto_lock_ = config->get<bool>("/security/auto_lock", false);
}

void LockManager::save_to_config() {
    auto* config = Config::get_instance();
    if (!config) return;
    config->set<std::string>("/security/pin_hash", pin_hash_);
    config->set<bool>("/security/auto_lock", auto_lock_);
    config->save();
}

} // namespace helix
```

- [ ] **Step 3: Run tests — verify they pass**

```bash
make test && ./build/bin/helix-tests "[lock]" -v
```

Expected: all 7 test cases pass.

- [ ] **Step 4: Commit**

```bash
git add include/lock_manager.h src/system/lock_manager.cpp
git commit -m "feat(lock): add LockManager with PIN hash and lock state"
```

---

## Chunk 2: Lock Screen Overlay UI

### Task 4: Create lock screen XML component

**Files:**
- Create: `ui_xml/components/lock_screen.xml`

Read `docs/devel/LVGL9_XML_GUIDE.md` and `docs/devel/UI_CONTRIBUTOR_GUIDE.md` for XML patterns and design tokens. Check existing overlay XML files (e.g., `ui_xml/overlay_backdrop.xml`, `ui_xml/components/panel_widget_shutdown.xml`) for styling conventions.

- [ ] **Step 1: Write lock screen XML**

The layout:
- Full-screen black background (opaque)
- Centered card with lock icon, "Enter PIN" heading, dot indicators, numeric keypad
- E-Stop FAB in top-right corner (same position as panel E-Stop buttons)
- Printer name at top for context

Key elements:
- `lock_dots` container: 6 `lv_obj` circles (filled/empty based on digits entered)
- `lock_keypad`: 4x3 grid of buttons (1-9, backspace, 0, confirm)
- `lock_estop`: E-Stop FAB using same `emergency_stop_clicked` callback as panel E-Stop buttons. Must respect `SafetySettingsManager::get_estop_require_confirmation()` — the existing callback already handles this, so reusing it is sufficient. Bind visibility to `estop_visible` subject (same as panel E-Stop buttons) so it only shows during printing/paused states.
- `lock_error_label`: hidden error text, shown on wrong PIN

Use design tokens: `#space_md` for padding, `#card_bg` for card background, semantic text widgets (`text_heading`, `text_body`), `ui_button` for keypad buttons.

- [ ] **Step 2: Register XML component in `main.cpp`**

Add `lv_xml_component_register_from_file()` call. Applying [L014].

- [ ] **Step 3: Commit**

```bash
git add ui_xml/components/lock_screen.xml src/main.cpp
git commit -m "feat(lock): add lock screen XML component"
```

### Task 5: Implement lock screen overlay class

**Files:**
- Create: `include/ui_lock_screen.h`
- Create: `src/ui/ui_lock_screen.cpp`

- [ ] **Step 1: Write lock screen header**

Class `LockScreenOverlay`:
- `show()` / `hide()` — create/destroy overlay on `lv_layer_top()`
- `is_visible()` — check if overlay is active
- Private: digit buffer (up to 6 chars), dot update, keypad callbacks
- Singleton pattern (like `get_sound_settings_overlay()`)
- Register with `StaticPanelRegistry` for cleanup

- [ ] **Step 2: Write lock screen implementation**

Key behaviors:
- Create overlay programmatically on `lv_layer_top()` with XML component inside
- Keypad digit buttons append to digit buffer, update dot indicators (filled circle = entered digit)
- Backspace removes last digit
- Confirm button (or auto-submit at max length) calls `LockManager::try_unlock()`
- Wrong PIN: shake animation on dot container (translate X oscillation via `lv_anim_t`), 500ms delay, clear digits
- Correct PIN: call `hide()`, signal DisplayManager to fully wake
- E-Stop button uses existing `emergency_stop_clicked` callback
- All keypad callbacks use `lv_xml_register_event_cb()` pattern (NOT `lv_obj_add_event_cb`)

Find the `emergency_stop_clicked` callback registration to ensure it's available. Reference the shutdown widget pattern for safe event callback structure (`LVGL_SAFE_EVENT_CB_BEGIN`/`END`).

- [ ] **Step 3: Build and verify compilation**

```bash
make -j
```

- [ ] **Step 4: Manual test**

```bash
./build/bin/helix-screen --test -vv
```

Test: set a PIN via LockManager API (or temporarily hardcode), call `LockScreenOverlay::show()` on startup, verify keypad appears, digits work, correct PIN dismisses.

- [ ] **Step 5: Commit**

```bash
git add include/ui_lock_screen.h src/ui/ui_lock_screen.cpp
git commit -m "feat(lock): implement lock screen overlay with numeric keypad"
```

---

## Chunk 3: Lock Panel Widget

### Task 6: Create lock widget

**Files:**
- Create: `ui_xml/components/panel_widget_lock.xml`
- Create: `src/ui/panel_widgets/lock_widget.h`
- Create: `src/ui/panel_widgets/lock_widget.cpp`
- Modify: `src/ui/panel_widget_registry.cpp` — add widget def and registration

- [ ] **Step 1: Write widget XML**

Follow the `panel_widget_shutdown.xml` pattern:
- `ui_button` with `variant="ghost"`, full width/height
- Lock icon centered
- `event_cb` trigger="clicked" callback="lock_screen_clicked_cb"
- `bind_flag_if_eq` to hide when `lock_pin_set` subject is 0

- [ ] **Step 2: Write widget C++ class**

Follow `shutdown_widget.cpp` pattern:
- `LockWidget` extends `PanelWidget`
- `attach()`: find button, set user data
- `detach()`: clear user data
- Static `lock_screen_clicked_cb`: calls `LockManager::lock()` then `LockScreenOverlay::show()`
- `register_lock_widget()`: register factory + XML event callback

- [ ] **Step 3: Add to widget registry**

In `panel_widget_registry.cpp`:
- Add `PanelWidgetDef` entry: id=`"lock"`, display_name=`"Lock Screen"`, icon=`"lock"`, description=`"PIN-protected screen lock"`, default_enabled=false, 1x1, not resizable (max 1x1)
- No hardware gate — visibility controlled by `lock_pin_set` subject in XML
- Add `void register_lock_widget();` forward declaration
- Call `register_lock_widget()` in `init_widget_registrations()`

- [ ] **Step 4: Implement `LockManager::init_subjects()` with proper lifecycle**

Add `init_subjects()` to `LockManager` following the mandatory pattern from CLAUDE.md:
- Call `lv_subject_init_int(&pin_set_subject_, has_pin() ? 1 : 0)`
- Register globally: `lv_xml_register_subject(nullptr, "lock_pin_set", &pin_set_subject_)`
- Self-register cleanup with `StaticSubjectRegistry`:
  ```cpp
  StaticSubjectRegistry::instance().register_deinit(
      "LockManager", []() { LockManager::instance().deinit_subjects(); });
  ```
- In `deinit_subjects()`: call `lv_subject_deinit(&pin_set_subject_)`
- Update subject in `set_pin()` and `remove_pin()`: `lv_subject_set_int(&pin_set_subject_, has_pin() ? 1 : 0)`
- Call `init_subjects()` from `subject_initializer.cpp` alongside other singleton subject inits

- [ ] **Step 5: Register XML component in `main.cpp`**

Applying [L014]: add `lv_xml_component_register_from_file()` for `panel_widget_lock`.

- [ ] **Step 6: Build and test**

```bash
make -j && ./build/bin/helix-screen --test -vv
```

Verify: widget appears in catalog only when PIN is set. Tapping locks screen.

- [ ] **Step 7: Commit**

```bash
git add ui_xml/components/panel_widget_lock.xml \
       src/ui/panel_widgets/lock_widget.h \
       src/ui/panel_widgets/lock_widget.cpp \
       src/ui/panel_widget_registry.cpp \
       src/main.cpp
git commit -m "feat(lock): add Lock Screen panel widget"
```

---

## Chunk 4: Security Settings

### Task 7: Create security settings overlay

**Files:**
- Create: `ui_xml/security_settings_overlay.xml`
- Create: `include/ui_settings_security.h`
- Create: `src/ui/ui_settings_security.cpp`
- Modify: `src/ui/ui_panel_settings.cpp` — add Security row
- Modify: `src/main.cpp` — register XML component

- [ ] **Step 1: Write settings XML**

Follow existing settings overlay patterns (e.g., `sound_settings_overlay.xml`). Layout:

- Navigation header with back button and "Security" title
- **Set PIN** section (visible when no PIN set):
  - "Set PIN" button → opens PIN entry flow
- **PIN Management** section (visible when PIN is set):
  - "Change PIN" button
  - "Remove PIN" button
- **Auto-Lock** toggle (visible when PIN is set):
  - Toggle switch bound to `auto_lock_enabled` subject
  - Description: "Lock screen automatically after idle timeout"

Use `bind_flag_if_eq` / `bind_flag_if_not_eq` on `lock_pin_set` subject to toggle section visibility.

- [ ] **Step 2: Create a reusable PIN entry modal**

Create `ui_xml/components/pin_entry_modal.xml` and `src/ui/ui_pin_entry_modal.cpp`. This is a lightweight modal (not the full lock screen overlay) used exclusively by the security settings for PIN setup flows. It contains:
- A heading label (dynamically set: "Enter New PIN", "Confirm PIN", "Enter Current PIN")
- 6 dot indicators + numeric keypad (same layout as lock screen, extracted as shared pattern)
- Cancel button

The modal exposes a callback-based API:
```cpp
// Show modal, call on_complete with entered PIN (or empty string if cancelled)
void show_pin_entry(const std::string& heading,
                    std::function<void(const std::string& pin)> on_complete);
```

**Set PIN flow** (called from "Set PIN" button):
1. `show_pin_entry("Enter New PIN", [](pin1) {`
2. `show_pin_entry("Confirm PIN", [pin1](pin2) {`
3. If `pin1 == pin2`: `LockManager::set_pin(pin1)`, show success toast
4. If mismatch: show error toast "PINs don't match", restart flow

**Change PIN flow** (called from "Change PIN" button):
1. `show_pin_entry("Enter Current PIN", [](current) {`
2. If `!LockManager::verify_pin(current)`: show error toast "Wrong PIN"
3. If correct: run Set PIN flow (steps 1-4 above)

**Remove PIN flow** (called from "Remove PIN" button):
1. `show_pin_entry("Enter Current PIN", [](current) {`
2. If `!LockManager::verify_pin(current)`: show error toast "Wrong PIN"
3. If correct: `LockManager::remove_pin()`, show success toast

Register XML component in `main.cpp` (applying [L014]).

- [ ] **Step 3: Add Security row to Settings panel**

In `ui_panel_settings.cpp`, add a settings row for "Security" (with lock icon) that opens the security settings overlay. Check the existing settings panel layout in `ui_xml/settings_panel.xml` to find the right placement — look for where safety-related settings (e.g., E-Stop confirmation) live and place the Security row nearby. Follow the pattern of existing rows.

- [ ] **Step 4: Register XML component**

Applying [L014].

- [ ] **Step 5: Build and test**

```bash
make -j && ./build/bin/helix-screen --test -vv -p settings
```

Test: navigate to Settings → Security. Set a PIN, change it, remove it. Toggle auto-lock.

- [ ] **Step 6: Commit**

```bash
git add ui_xml/security_settings_overlay.xml \
       include/ui_settings_security.h \
       src/ui/ui_settings_security.cpp \
       src/ui/ui_panel_settings.cpp \
       src/main.cpp
git commit -m "feat(lock): add Security settings for PIN management"
```

---

## Chunk 5: Auto-Lock & DisplayManager Integration

### Task 8: Hook auto-lock into DisplayManager wake flow

**Files:**
- Modify: `src/application/display_manager.cpp` — `wake_display()` method

- [ ] **Step 1: Modify wake_display()**

In `DisplayManager::wake_display()`, add the auto-lock check **after** brightness is restored (after the `m_backlight->set_brightness()` call) but **before** the sleep callback notifications. `wake_display()` runs on the main LVGL thread, so no `ui_queue_update()` needed.

```cpp
// After brightness restore, before sleep callback notifications:
// Auto-lock: show lock screen if PIN set and auto-lock enabled
if (was_sleeping &&
    helix::LockManager::instance().auto_lock_enabled() &&
    helix::LockManager::instance().has_pin()) {
    helix::LockManager::instance().lock();
    get_lock_screen_overlay().show();
}
```

- [ ] **Step 2: Build and test**

```bash
make -j && ./build/bin/helix-screen --test -vv
```

Test: set a PIN, enable auto-lock in settings, let the screensaver activate, tap to wake — lock screen should appear instead of direct wake.

- [ ] **Step 3: Commit**

```bash
git add src/application/display_manager.cpp
git commit -m "feat(lock): auto-lock on wake from screensaver/sleep"
```

### Task 9: Update home panel widget guide

**Files:**
- Modify: `docs/user/guide/home-panel.md`

- [ ] **Step 1: Add Lock Screen widget to docs**

Add to:
- Complete Widget Reference table: Lock Screen, 1x1, not resizable, requires PIN set in Security settings
- Widget Interactions table: "Lock Screen — Locks the screen immediately"
- Note in the description that the widget only appears in the catalog after setting a PIN in Settings → Security

- [ ] **Step 2: Commit**

```bash
git add docs/user/guide/home-panel.md
git commit -m "docs(user): add Lock Screen widget to home panel guide"
```

### Task 10: Final integration test

- [ ] **Step 1: Full flow test**

```bash
make -j && ./build/bin/helix-screen --test -vv
```

Test the full flow:
1. Settings → Security → Set PIN (e.g., "1234")
2. Verify auto-lock toggle appears
3. Go to Home Panel → Edit Mode → add Lock Screen widget
4. Exit edit mode → tap Lock widget → lock screen appears
5. Enter wrong PIN → dots shake, retry
6. Enter correct PIN → screen unlocks
7. Enable auto-lock → let screensaver activate → wake → lock screen appears
8. E-Stop button on lock screen works (sends emergency stop)
9. Settings → Security → Remove PIN → lock widget disappears from grid

- [ ] **Step 2: Run unit tests**

```bash
make test-run
```

Verify no regressions.
