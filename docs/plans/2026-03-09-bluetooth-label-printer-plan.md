# Bluetooth Label Printer Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add Bluetooth (Classic SPP + BLE GATT) as a runtime-loadable label printer transport via a shared library plugin.

**Architecture:** Shared library plugin (`libhelix-bluetooth.so`) loaded via `dlopen()` at runtime. Zero footprint when no BT hardware. Protocol byte generation extracted into shared pure functions so USB, TCP, and BT transports all use the same codepath.

**Tech Stack:** BlueZ D-Bus via sd-bus (libsystemd), RFCOMM sockets (libbluetooth), LVGL XML UI, existing ILabelPrinter interface.

**Design Doc:** `docs/plans/2026-03-09-bluetooth-label-printer-design.md`

---

## Phase 1: Protocol Extraction Refactor

Extract protocol byte generation from existing printer classes into pure functions. This is a prerequisite — no new dependencies, no behavioral changes, fully testable.

### Task 1: Extract Brother QL Protocol

**Files:**
- Create: `include/brother_ql_protocol.h`
- Create: `src/system/brother_ql_protocol.cpp`
- Modify: `src/system/brother_ql_printer.cpp` (lines 58–179 → extract, lines 181–245 → call extracted fn)
- Create: `tests/test_brother_ql_protocol.cpp`

**Step 1: Write failing tests for protocol extraction**

Read `src/system/brother_ql_printer.cpp` lines 58–179 to understand the `build_raster_commands()` method. Write tests that verify the extracted function produces correct byte sequences:

```cpp
// tests/test_brother_ql_protocol.cpp
#include <catch2/catch_test_macros.hpp>
#include "brother_ql_protocol.h"
#include "label_bitmap.h"

using namespace helix::label;

TEST_CASE("Brother QL protocol - invalidation header", "[label][brother]") {
    LabelBitmap bitmap(696, 200);  // 62mm width
    LabelSize size{62, 0, "62mm", true};  // circular die-cut

    auto data = brother_ql_build_raster(bitmap, size);

    // First 200 bytes must be 0x00 (invalidation)
    REQUIRE(data.size() > 200);
    for (int i = 0; i < 200; i++) {
        REQUIRE(data[i] == 0x00);
    }
}

TEST_CASE("Brother QL protocol - init sequence", "[label][brother]") {
    LabelBitmap bitmap(696, 200);
    LabelSize size{62, 0, "62mm", true};

    auto data = brother_ql_build_raster(bitmap, size);

    // After 200 bytes of invalidation: ESC @ (initialize)
    REQUIRE(data[200] == 0x1B);
    REQUIRE(data[201] == 0x40);
}

TEST_CASE("Brother QL protocol - print command at end", "[label][brother]") {
    LabelBitmap bitmap(696, 200);
    LabelSize size{62, 0, "62mm", true};

    auto data = brother_ql_build_raster(bitmap, size);

    // Last byte is 0x1A (print command)
    REQUIRE(data.back() == 0x1A);
}

TEST_CASE("Brother QL protocol - deterministic output", "[label][brother]") {
    LabelBitmap bitmap(696, 100);
    LabelSize size{62, 0, "62mm", true};

    auto data1 = brother_ql_build_raster(bitmap, size);
    auto data2 = brother_ql_build_raster(bitmap, size);

    REQUIRE(data1 == data2);
}
```

**Step 2: Run tests to verify they fail**

```bash
make test && ./build/bin/helix-tests "[brother]" -v
```

Expected: Compilation failure — `brother_ql_protocol.h` doesn't exist.

**Step 3: Create protocol header and implementation**

Create `include/brother_ql_protocol.h`:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "label_bitmap.h"

#include <cstdint>
#include <vector>

namespace helix::label {

/// Build Brother QL raster protocol bytes from a bitmap.
/// Pure function — no I/O. Output can be sent over TCP, RFCOMM, or any transport.
std::vector<uint8_t> brother_ql_build_raster(const LabelBitmap& bitmap,
                                              const LabelSize& size);

}  // namespace helix::label
```

Create `src/system/brother_ql_protocol.cpp` — move the body of `BrotherQLPrinter::build_raster_commands()` (lines 58–179 of `brother_ql_printer.cpp`) into the free function `brother_ql_build_raster()`. Keep the same logic, just change the function signature.

**Step 4: Update BrotherQLPrinter to call extracted function**

Modify `src/system/brother_ql_printer.cpp`:
- Add `#include "brother_ql_protocol.h"`
- Replace `build_raster_commands()` body with: `return helix::label::brother_ql_build_raster(bitmap_, size_);`
- Or remove `build_raster_commands()` entirely and call the free function directly in `print_label()`

**Step 5: Run tests to verify they pass**

```bash
make test && ./build/bin/helix-tests "[brother]" -v
```

Expected: All 4 tests PASS.

**Step 6: Run existing tests to verify no regressions**

```bash
make test-run
```

Expected: All existing tests still pass.

**Step 7: Commit**

```bash
git add include/brother_ql_protocol.h src/system/brother_ql_protocol.cpp \
        src/system/brother_ql_printer.cpp tests/test_brother_ql_protocol.cpp
git commit -m "refactor(label): extract Brother QL protocol into pure function"
```

---

### Task 2: Extract Phomemo Protocol

**Files:**
- Create: `include/phomemo_protocol.h`
- Create: `src/system/phomemo_protocol.cpp`
- Modify: `src/system/phomemo_printer.cpp` (lines 50–114 → extract, lines 145–194 → call extracted fn)
- Create: `tests/test_phomemo_protocol.cpp`

**Step 1: Write failing tests for protocol extraction**

Read `src/system/phomemo_printer.cpp` lines 50–114 to understand `build_raster_commands()`. Write tests:

```cpp
// tests/test_phomemo_protocol.cpp
#include <catch2/catch_test_macros.hpp>
#include "phomemo_protocol.h"
#include "label_bitmap.h"

using namespace helix::label;

TEST_CASE("Phomemo protocol - GS v 0 raster command", "[label][phomemo]") {
    LabelBitmap bitmap(384, 240);  // 48mm × 30mm at 203dpi
    LabelSize size{40, 30, "40x30mm", false};

    auto data = phomemo_build_raster(bitmap, size);

    // Must contain GS v 0 command (0x1D 0x76 0x30)
    bool found = false;
    for (size_t i = 0; i + 2 < data.size(); i++) {
        if (data[i] == 0x1D && data[i+1] == 0x76 && data[i+2] == 0x30) {
            found = true;
            break;
        }
    }
    REQUIRE(found);
}

TEST_CASE("Phomemo protocol - finalize sequence", "[label][phomemo]") {
    LabelBitmap bitmap(384, 240);
    LabelSize size{40, 30, "40x30mm", false};

    auto data = phomemo_build_raster(bitmap, size);

    // Must end with finalize (0x1F 0xF0 0x05 0x00) + feed (0x1F 0xF0 0x03 0x00)
    REQUIRE(data.size() >= 8);
    // Check feed-to-gap at end
    size_t n = data.size();
    REQUIRE(data[n-4] == 0x1F);
    REQUIRE(data[n-3] == 0xF0);
    REQUIRE(data[n-2] == 0x03);
    REQUIRE(data[n-1] == 0x00);
}

TEST_CASE("Phomemo protocol - deterministic output", "[label][phomemo]") {
    LabelBitmap bitmap(384, 240);
    LabelSize size{40, 30, "40x30mm", false};

    auto data1 = phomemo_build_raster(bitmap, size);
    auto data2 = phomemo_build_raster(bitmap, size);

    REQUIRE(data1 == data2);
}
```

**Step 2: Run tests — expect compilation failure**

```bash
make test && ./build/bin/helix-tests "[phomemo]" -v
```

**Step 3: Create protocol header and implementation**

Create `include/phomemo_protocol.h`:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "label_bitmap.h"

#include <cstdint>
#include <vector>

namespace helix::label {

/// Build Phomemo ESC/POS raster protocol bytes from a bitmap.
/// Pure function — no I/O. Output can be sent over USB, BLE GATT, or any transport.
std::vector<uint8_t> phomemo_build_raster(const LabelBitmap& bitmap,
                                           const LabelSize& size);

}  // namespace helix::label
```

Create `src/system/phomemo_protocol.cpp` — move body of `PhomemoPrinter::build_raster_commands()` (lines 50–114 of `phomemo_printer.cpp`) into `phomemo_build_raster()`.

**Step 4: Update PhomemoPrinter to call extracted function**

Modify `src/system/phomemo_printer.cpp`:
- Add `#include "phomemo_protocol.h"`
- Replace `build_raster_commands()` body or call the free function directly in `print()`

**Step 5: Run all tests**

```bash
make test-run
```

Expected: All tests pass including new Phomemo protocol tests + existing tests.

**Step 6: Commit**

```bash
git add include/phomemo_protocol.h src/system/phomemo_protocol.cpp \
        src/system/phomemo_printer.cpp tests/test_phomemo_protocol.cpp
git commit -m "refactor(label): extract Phomemo protocol into pure function"
```

---

## Phase 2: Plugin ABI & Loader (Main Binary Side)

### Task 3: Define Plugin C ABI Header

**Files:**
- Create: `include/bluetooth_plugin.h`

**Step 1: Create the C ABI header**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HELIX_BT_API_VERSION 1

/// Plugin metadata
typedef struct {
    int api_version;
    const char* name;
    bool has_classic;  ///< Supports SPP/RFCOMM
    bool has_ble;      ///< Supports BLE GATT
} helix_bt_plugin_info;

/// Discovered device
typedef struct {
    const char* mac;           ///< "AA:BB:CC:DD:EE:FF"
    const char* name;          ///< Human-readable name
    bool paired;
    bool is_ble;               ///< false=Classic, true=BLE
    const char* service_uuid;  ///< Primary service UUID (SPP or vendor)
} helix_bt_device;

/// Opaque context
typedef struct helix_bt_context helix_bt_context;

/// Discovery callback — called per device found. user_data is pass-through.
typedef void (*helix_bt_discover_cb)(const helix_bt_device* dev, void* user_data);

// --- Required plugin exports ---

/// Return plugin metadata. Must return api_version == HELIX_BT_API_VERSION.
typedef helix_bt_plugin_info* (*helix_bt_get_info_fn)(void);

/// Initialize plugin. Starts internal sd-bus event loop thread.
typedef helix_bt_context* (*helix_bt_init_fn)(void);

/// Shut down plugin. Stops event loop, frees resources.
typedef void (*helix_bt_deinit_fn)(helix_bt_context*);

/// Start discovery. Calls cb per device found. Stops after timeout_ms.
/// Returns 0 on success, negative on error.
typedef int (*helix_bt_discover_fn)(helix_bt_context*, int timeout_ms,
                                    helix_bt_discover_cb cb, void* user_data);

/// Stop an in-progress discovery early.
typedef void (*helix_bt_stop_discovery_fn)(helix_bt_context*);

/// Pair with a device. Blocks until pairing completes or fails.
/// Returns 0 on success, negative on error.
typedef int (*helix_bt_pair_fn)(helix_bt_context*, const char* mac);

/// Check if device is paired. Returns 1=paired, 0=not paired, negative=error.
typedef int (*helix_bt_is_paired_fn)(helix_bt_context*, const char* mac);

/// Connect via RFCOMM (SPP). Returns fd on success, negative on error.
typedef int (*helix_bt_connect_rfcomm_fn)(helix_bt_context*, const char* mac, int channel);

/// Connect via BLE GATT. Returns handle on success, negative on error.
typedef int (*helix_bt_connect_ble_fn)(helix_bt_context*, const char* mac,
                                       const char* write_uuid);

/// Write data to BLE GATT characteristic. Handles chunking to MTU internally.
/// Returns 0 on success, negative on error.
typedef int (*helix_bt_ble_write_fn)(helix_bt_context*, int handle,
                                     const uint8_t* data, int len);

/// Disconnect a connection (RFCOMM fd or BLE handle).
typedef void (*helix_bt_disconnect_fn)(helix_bt_context*, int handle);

/// Get human-readable error string for last failure.
typedef const char* (*helix_bt_last_error_fn)(helix_bt_context*);

#ifdef __cplusplus
}
#endif
```

**Step 2: Commit**

```bash
git add include/bluetooth_plugin.h
git commit -m "feat(bluetooth): define plugin C ABI header"
```

---

### Task 4: Implement BluetoothLoader (dlopen Wrapper)

**Files:**
- Create: `include/bluetooth_loader.h`
- Create: `src/system/bluetooth_loader.cpp`
- Create: `tests/test_bluetooth_loader.cpp`

**Step 1: Write failing tests**

```cpp
// tests/test_bluetooth_loader.cpp
#include <catch2/catch_test_macros.hpp>
#include "bluetooth_loader.h"

using namespace helix::bluetooth;

TEST_CASE("BluetoothLoader - unavailable when .so missing", "[bluetooth][loader]") {
    // Default state: no plugin loaded
    auto& loader = BluetoothLoader::instance();
    // On dev machines without the .so, this should be false
    // (This test is environment-dependent — it documents the expected behavior)
    // The loader should not crash or throw when .so is missing
    REQUIRE_NOTHROW(loader.is_available());
}

TEST_CASE("BluetoothLoader - singleton consistency", "[bluetooth][loader]") {
    auto& a = BluetoothLoader::instance();
    auto& b = BluetoothLoader::instance();
    REQUIRE(&a == &b);
}
```

**Step 2: Run tests — expect compilation failure**

```bash
make test && ./build/bin/helix-tests "[bluetooth][loader]" -v
```

**Step 3: Implement BluetoothLoader**

Create `include/bluetooth_loader.h`:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "bluetooth_plugin.h"

#include <string>

namespace helix::bluetooth {

/// Runtime loader for libhelix-bluetooth.so.
/// Checks for BT hardware, loads plugin via dlopen, resolves function pointers.
/// Zero overhead when BT unavailable — is_available() returns false, all ops are no-ops.
class BluetoothLoader {
  public:
    static BluetoothLoader& instance();

    /// Check if BT hardware exists AND plugin loaded successfully.
    [[nodiscard]] bool is_available() const;

    /// Plugin function pointers — only valid when is_available() == true.
    /// Caller must check is_available() before using these.
    helix_bt_init_fn init = nullptr;
    helix_bt_deinit_fn deinit = nullptr;
    helix_bt_discover_fn discover = nullptr;
    helix_bt_stop_discovery_fn stop_discovery = nullptr;
    helix_bt_pair_fn pair = nullptr;
    helix_bt_is_paired_fn is_paired = nullptr;
    helix_bt_connect_rfcomm_fn connect_rfcomm = nullptr;
    helix_bt_connect_ble_fn connect_ble = nullptr;
    helix_bt_ble_write_fn ble_write = nullptr;
    helix_bt_disconnect_fn disconnect = nullptr;
    helix_bt_last_error_fn last_error = nullptr;

    // Non-copyable
    BluetoothLoader(const BluetoothLoader&) = delete;
    BluetoothLoader& operator=(const BluetoothLoader&) = delete;

  private:
    BluetoothLoader();
    ~BluetoothLoader();

    bool try_load();
    static bool has_bt_hardware();

    void* dl_handle_ = nullptr;
    bool available_ = false;
};

}  // namespace helix::bluetooth
```

Create `src/system/bluetooth_loader.cpp`:
- Constructor: call `has_bt_hardware()` → if false, return. Call `try_load()` → set `available_`.
- `has_bt_hardware()`: check `access("/sys/class/bluetooth/hci0", F_OK) == 0`
- `try_load()`: `dlopen("libhelix-bluetooth.so", RTLD_NOW)` from executable directory. Resolve all function pointers via `dlsym()`. Verify API version via `get_info()->api_version == HELIX_BT_API_VERSION`. Return false on any failure.
- Destructor: `dlclose()` if handle is open.
- Log all steps with spdlog: hardware check result, dlopen success/fail, API version match.

**Step 4: Run tests**

```bash
make test && ./build/bin/helix-tests "[bluetooth][loader]" -v
```

Expected: PASS (loader gracefully handles missing .so on dev machines).

**Step 5: Commit**

```bash
git add include/bluetooth_loader.h src/system/bluetooth_loader.cpp \
        tests/test_bluetooth_loader.cpp
git commit -m "feat(bluetooth): implement BluetoothLoader with dlopen"
```

---

## Phase 3: Settings & Configuration

### Task 5: Add Bluetooth Fields to Settings

**Files:**
- Modify: `include/label_printer_settings.h` (add bt_address, bt_transport getters/setters after line 93)
- Modify: `src/system/label_printer_settings.cpp` (add persistence, update is_configured ~line 190)
- Create: `tests/test_label_printer_settings_bt.cpp`

**Step 1: Write failing tests**

```cpp
// tests/test_label_printer_settings_bt.cpp
#include <catch2/catch_test_macros.hpp>
#include "label_printer_settings.h"

using namespace helix::label;

TEST_CASE("Label printer settings - bluetooth type", "[label][settings]") {
    auto& settings = LabelPrinterSettingsManager::instance();
    settings.set_printer_type("bluetooth");
    REQUIRE(settings.get_printer_type() == "bluetooth");
}

TEST_CASE("Label printer settings - bt_address persistence", "[label][settings]") {
    auto& settings = LabelPrinterSettingsManager::instance();
    settings.set_bt_address("AA:BB:CC:DD:EE:FF");
    REQUIRE(settings.get_bt_address() == "AA:BB:CC:DD:EE:FF");
}

TEST_CASE("Label printer settings - bt_transport persistence", "[label][settings]") {
    auto& settings = LabelPrinterSettingsManager::instance();
    settings.set_bt_transport("spp");
    REQUIRE(settings.get_bt_transport() == "spp");

    settings.set_bt_transport("ble");
    REQUIRE(settings.get_bt_transport() == "ble");
}

TEST_CASE("Label printer settings - BT configured check", "[label][settings]") {
    auto& settings = LabelPrinterSettingsManager::instance();
    settings.set_printer_type("bluetooth");

    settings.set_bt_address("");
    REQUIRE_FALSE(settings.is_configured());

    settings.set_bt_address("AA:BB:CC:DD:EE:FF");
    REQUIRE(settings.is_configured());
}
```

**Step 2: Run tests — expect compilation failure**

```bash
make test && ./build/bin/helix-tests "[label][settings]" -v
```

**Step 3: Add BT fields to settings header**

Modify `include/label_printer_settings.h` — add after USB fields (~line 93):

```cpp
    // Bluetooth settings
    [[nodiscard]] std::string get_bt_address() const;
    void set_bt_address(const std::string& address);

    [[nodiscard]] std::string get_bt_transport() const;  // "spp" or "ble"
    void set_bt_transport(const std::string& transport);
```

**Step 4: Implement in settings .cpp**

Modify `src/system/label_printer_settings.cpp`:
- Add getters/setters for `/label_printer/bt_address` and `/label_printer/bt_transport`
- Follow exact pattern of existing USB VID/PID getters (lines 143–188)
- Update `is_configured()` (~line 190) to handle `type == "bluetooth"`

**Step 5: Run all tests**

```bash
make test-run
```

Expected: All tests pass.

**Step 6: Commit**

```bash
git add include/label_printer_settings.h src/system/label_printer_settings.cpp \
        tests/test_label_printer_settings_bt.cpp
git commit -m "feat(label): add Bluetooth address/transport settings fields"
```

---

## Phase 4: Bluetooth Printer Backends (Main Binary Side)

### Task 6: BrotherQLBluetoothPrinter

**Files:**
- Create: `include/brother_ql_bt_printer.h`
- Create: `src/system/brother_ql_bt_printer.cpp`

**Step 1: Implement BrotherQLBluetoothPrinter**

This class implements `ILabelPrinter`, uses `BluetoothLoader` for RFCOMM, and `brother_ql_build_raster()` for protocol. Pattern follows `BrotherQLPrinter` (lines 181–245 of `brother_ql_printer.cpp`) but replaces TCP socket with RFCOMM fd.

```cpp
// include/brother_ql_bt_printer.h
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "label_printer.h"

#include <string>

namespace helix::label {

class BrotherQLBluetoothPrinter : public ILabelPrinter {
  public:
    void set_device(const std::string& mac, int channel = 1);

    [[nodiscard]] std::string name() const override;
    void print(const LabelBitmap& bitmap, const LabelSize& size,
               PrintCallback callback) override;
    [[nodiscard]] std::vector<LabelSize> supported_sizes() const override;

  private:
    std::string mac_;
    int channel_ = 1;
};

}  // namespace helix::label
```

Implementation:
- `print()`: Build raster via `brother_ql_build_raster()`, spawn detached thread, call `BluetoothLoader::instance().connect_rfcomm(mac_, channel_)` → get fd, `::write()` the raster bytes to fd, `::close(fd)`, callback via `queue_update()`
- `supported_sizes()`: Return same sizes as `BrotherQLPrinter::supported_sizes()`

**Step 2: Commit**

```bash
git add include/brother_ql_bt_printer.h src/system/brother_ql_bt_printer.cpp
git commit -m "feat(bluetooth): add BrotherQLBluetoothPrinter backend"
```

---

### Task 7: PhomemoBluetoothPrinter

**Files:**
- Create: `include/phomemo_bt_printer.h`
- Create: `src/system/phomemo_bt_printer.cpp`

**Step 1: Implement PhomemoBluetoothPrinter**

Same pattern as Task 6 but uses BLE GATT via `connect_ble()` + `ble_write()`.

```cpp
// include/phomemo_bt_printer.h
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "label_printer.h"

#include <string>

namespace helix::label {

class PhomemoBluetoothPrinter : public ILabelPrinter {
  public:
    void set_device(const std::string& mac);

    [[nodiscard]] std::string name() const override;
    void print(const LabelBitmap& bitmap, const LabelSize& size,
               PrintCallback callback) override;
    [[nodiscard]] std::vector<LabelSize> supported_sizes() const override;

  private:
    std::string mac_;
    static constexpr const char* PHOMEMO_WRITE_UUID = "0000ff02-0000-1000-8000-00805f9b34fb";
};

}  // namespace helix::label
```

Implementation:
- `print()`: Build raster via `phomemo_build_raster()`, spawn detached thread, call `connect_ble(mac_, PHOMEMO_WRITE_UUID)` → handle, call `ble_write(handle, data.data(), data.size())` (plugin handles MTU chunking), `disconnect(handle)`, callback via `queue_update()`
- `supported_sizes()`: Return same sizes as `PhomemoPrinter::supported_sizes()`

**Step 2: Commit**

```bash
git add include/phomemo_bt_printer.h src/system/phomemo_bt_printer.cpp
git commit -m "feat(bluetooth): add PhomemoBluetoothPrinter backend"
```

---

### Task 8: Wire BT Backends into print_spool_label()

**Files:**
- Modify: `src/system/label_printer_utils.cpp` (lines 19–71)

**Step 1: Add BT branch to print_spool_label()**

Read `src/system/label_printer_utils.cpp` carefully. Modify the type selection logic (~line 21) to handle `"bluetooth"`:

```cpp
const std::string type = settings.get_printer_type();
const bool is_usb = (type == "usb");
const bool is_bt = (type == "bluetooth");
```

Add a new `else if (is_bt)` branch (~line 66) that:
1. Gets `bt_transport` from settings ("spp" or "ble")
2. If "spp": use static `BrotherQLBluetoothPrinter`, call `set_device(bt_address)`, print
3. If "ble": use static `PhomemoBluetoothPrinter`, call `set_device(bt_address)`, print
4. Select label sizes from the appropriate printer's `supported_sizes()`

**Step 2: Build and verify compilation**

```bash
make -j
```

**Step 3: Commit**

```bash
git add src/system/label_printer_utils.cpp
git commit -m "feat(bluetooth): wire BT printer backends into print_spool_label()"
```

---

## Phase 5: UI Changes

### Task 9: Update XML Layout for Bluetooth Section

**Files:**
- Modify: `ui_xml/label_printer_settings.xml`

**Step 1: Read the existing XML layout**

Read `ui_xml/label_printer_settings.xml` fully. Understand the existing Network (ref_value="0") and USB (ref_value="1") sections.

**Step 2: Add Bluetooth section**

After the USB section (~line 54), add a new Bluetooth section:

```xml
<!-- Bluetooth section: visible when type == 2 -->
<setting_section_header text="Bluetooth Printers"
    bind_flag_if_not_eq subject="printer_type_subject" flag="hidden" ref_value="2"/>

<lv_obj name="section_bluetooth" style_bg_opa="0" style_border_width="0"
    style_pad_all="0" width="content" height="content"
    style_layout="flex" style_flex_flow="column" style_pad_row="#space_sm"
    bind_flag_if_not_eq subject="printer_type_subject" flag="hidden" ref_value="2">

    <setting_dropdown_row name="row_bt_printers" label="Printer" options=""
        event_cb trigger="value_changed" callback="on_lp_bt_printer_selected"/>

    <ui_button name="btn_bt_scan" width="content" height="content"
        style_pad_all="#space_sm"
        event_cb trigger="clicked" callback="on_lp_bt_scan">
        <text_body text="Scan for Printers" clickable="false" event_bubble="true"/>
    </ui_button>
</lv_obj>
```

Also update the type dropdown options — this is handled in C++ (line 437), not XML.

Note: Applying [L071] — child elements of scan button need `clickable="false" event_bubble="true"`.
Note: Applying [L039] — callback names use `on_lp_bt_*` prefix for uniqueness.

**Step 3: Verify XML loads (no rebuild needed)**

Per [L031], XML changes don't need rebuild. Test with:
```bash
HELIX_HOT_RELOAD=1 ./build/bin/helix-screen --test -vv
```

**Step 4: Commit**

```bash
git add ui_xml/label_printer_settings.xml
git commit -m "feat(bluetooth): add BT section to label printer settings XML"
```

---

### Task 10: Update Settings Overlay C++ for Bluetooth

**Files:**
- Modify: `include/ui_settings_label_printer.h`
- Modify: `src/ui/ui_settings_label_printer.cpp`

**Step 1: Read the existing overlay implementation thoroughly**

Read both files completely. Understand:
- `init_printer_type_dropdown()` (line 427–443) — where to add "Bluetooth" option
- `handle_type_changed()` (line 445–469) — where to add BT branch
- Discovery lifecycle in `on_activate()`/`on_deactivate()`
- Callback registration (lines 80–91)

**Step 2: Add Bluetooth support to the overlay**

Changes needed:

1. **Header** — add new methods:
```cpp
    void init_bt_printer_dropdown();
    void start_bt_discovery();
    void stop_bt_discovery();
    void handle_bt_printer_selected(int index);
```

Add member variables:
```cpp
    lv_obj_t* bt_printers_dropdown_ = nullptr;
    helix_bt_context* bt_ctx_ = nullptr;
    std::vector<helix_bt_device> bt_devices_;
    bool bt_discovering_ = false;
```

2. **Type dropdown** (~line 437): Change format string to include "Bluetooth":
```cpp
auto options = fmt::format("{}\n{}\n{}", lv_tr("Network"), lv_tr("USB"), lv_tr("Bluetooth"));
```
Note: Applying [L067] — wrap user-visible strings in `lv_tr()`.

Only show "Bluetooth" option if `BluetoothLoader::instance().is_available()`:
```cpp
if (helix::bluetooth::BluetoothLoader::instance().is_available()) {
    options = fmt::format("{}\n{}\n{}", lv_tr("Network"), lv_tr("USB"), lv_tr("Bluetooth"));
} else {
    options = fmt::format("{}\n{}", lv_tr("Network"), lv_tr("USB"));
}
```

3. **Type changed** (~line 446): Handle index==2:
```cpp
std::string type;
if (index == 0) type = "network";
else if (index == 1) type = "usb";
else if (index == 2) type = "bluetooth";
```

4. **Discovery lifecycle**: Start/stop BT discovery in `on_activate()`/`on_deactivate()` when type is "bluetooth".

5. **Register callbacks** (~line 80): Add BT callbacks:
```cpp
{"on_lp_bt_printer_selected", on_lp_bt_printer_selected_cb},
{"on_lp_bt_scan", on_lp_bt_scan_cb},
```

6. **Pairing**: When user selects an unpaired device, show confirmation modal via `modal_show_confirmation()`. On confirm, call `BluetoothLoader::instance().pair(mac)` on a detached thread, show result toast.

**Step 3: Build and test**

```bash
make -j
./build/bin/helix-screen --test -vv
```

Navigate to Settings → Label Printer. Verify:
- With no BT hardware: only Network/USB shown
- Type dropdown correctly shows/hides sections

**Step 4: Commit**

```bash
git add include/ui_settings_label_printer.h src/ui/ui_settings_label_printer.cpp
git commit -m "feat(bluetooth): add BT discovery and pairing to label printer settings UI"
```

---

## Phase 6: Plugin Implementation (Shared Library)

### Task 11: Plugin Build Infrastructure

**Files:**
- Create: `src/bluetooth/` directory
- Modify: `Makefile` (add bluetooth-plugin target)

**Step 1: Add Makefile target**

Add near the end of the Makefile, before the help target:

```makefile
# --- Bluetooth Plugin (optional, runtime-loaded) ---
BT_PLUGIN_SRCS := $(wildcard src/bluetooth/*.cpp)
BT_PLUGIN_OBJS := $(BT_PLUGIN_SRCS:src/%.cpp=build/obj/%.o)

# Only build if dependencies available
BT_SYSTEMD_OK := $(shell pkg-config --exists libsystemd 2>/dev/null && echo "yes")
BT_BLUEZ_OK := $(shell pkg-config --exists bluez 2>/dev/null && echo "yes")

ifeq ($(BT_SYSTEMD_OK)-$(BT_BLUEZ_OK),yes-yes)
    BT_CXXFLAGS := $(shell pkg-config --cflags libsystemd) -fPIC -I$(INCLUDE_DIR)
    BT_LDFLAGS := $(shell pkg-config --libs libsystemd) -lbluetooth -shared
    BT_AVAILABLE := yes
else
    BT_AVAILABLE := no
endif

bluetooth-plugin: build/lib/libhelix-bluetooth.so

build/lib/libhelix-bluetooth.so: $(BT_PLUGIN_OBJS) | build/lib
	$(CXX) -o $@ $^ $(BT_LDFLAGS)

build/obj/bluetooth/%.o: src/bluetooth/%.cpp | build/obj/bluetooth
	$(CXX) $(CXXFLAGS_COMMON) $(BT_CXXFLAGS) -c -o $@ $<

build/obj/bluetooth build/lib:
	mkdir -p $@

.PHONY: bluetooth-plugin
```

**Step 2: Commit**

```bash
git add Makefile
git commit -m "build: add bluetooth-plugin Makefile target"
```

---

### Task 12: Plugin Core — Context & Event Loop

**Files:**
- Create: `src/bluetooth/bt_plugin.cpp`
- Create: `src/bluetooth/bt_context.h` (internal, not in include/)

**Step 1: Implement plugin core**

Create `src/bluetooth/bt_context.h` — internal context struct:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <systemd/sd-bus.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct helix_bt_context {
    sd_bus* bus = nullptr;
    std::thread event_thread;
    std::atomic<bool> running{false};
    std::mutex mutex;
    std::string last_error;

    // Discovery state
    std::atomic<bool> discovering{false};
    sd_bus_slot* discovery_slot = nullptr;
};
```

Create `src/bluetooth/bt_plugin.cpp` — C ABI exports:
- `helix_bt_get_info()`: Return static info struct with api_version=1, has_classic=true, has_ble=true
- `helix_bt_init()`: Create context, open system bus via `sd_bus_open_system()`, start event loop thread (`sd_bus_process` + `sd_bus_wait` loop)
- `helix_bt_deinit()`: Signal thread stop, join, close bus, free context
- `helix_bt_last_error()`: Return `ctx->last_error.c_str()`

**Step 2: Build plugin**

```bash
make bluetooth-plugin
```

Expected: `build/lib/libhelix-bluetooth.so` created.

**Step 3: Commit**

```bash
git add src/bluetooth/bt_context.h src/bluetooth/bt_plugin.cpp
git commit -m "feat(bluetooth): implement plugin core with sd-bus event loop"
```

---

### Task 13: Plugin Discovery — BlueZ D-Bus

**Files:**
- Create: `src/bluetooth/bt_discovery.cpp`

**Step 1: Implement discovery**

`helix_bt_discover()`:
1. Get adapter path: `sd_bus_call_method()` on `org.bluez` → `org.freedesktop.DBus.ObjectManager.GetManagedObjects` → find path with `org.bluez.Adapter1` interface
2. Add match for `InterfacesAdded` signal on `/` — fires when BlueZ discovers a new device
3. Call `org.bluez.Adapter1.StartDiscovery()` on the adapter
4. In signal handler: parse device properties (Address, Name, Paired, UUIDs), filter for label printer UUIDs (SPP `00001101-*` or Phomemo BLE UUIDs), call user's `discover_cb`
5. After `timeout_ms`: call `StopDiscovery()`

`helix_bt_stop_discovery()`:
- Call `org.bluez.Adapter1.StopDiscovery()`, remove signal match

Filter UUIDs:
- SPP: `00001101-0000-1000-8000-00805f9b34fb`
- Phomemo: `0000ff00-0000-1000-8000-00805f9b34fb` (service) / `0000ff02-*` (write char)

**Step 2: Build and test manually**

```bash
make bluetooth-plugin
# Manual test on BT-capable device:
# The main binary will dlopen this and call discover
```

**Step 3: Commit**

```bash
git add src/bluetooth/bt_discovery.cpp
git commit -m "feat(bluetooth): implement BlueZ D-Bus device discovery"
```

---

### Task 14: Plugin Pairing

**Files:**
- Modify: `src/bluetooth/bt_plugin.cpp` (add pair/is_paired exports)
- Create: `src/bluetooth/bt_pairing.cpp`

**Step 1: Implement pairing**

`helix_bt_pair()`:
1. Find device D-Bus path from MAC: `/org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF`
2. Call `org.bluez.Device1.Pair()` — blocks until complete
3. Return 0 on success, set last_error on failure

`helix_bt_is_paired()`:
1. Read `org.bluez.Device1.Paired` property via `sd_bus_get_property_trivial()`
2. Return 1 if paired, 0 if not

**Step 2: Build**

```bash
make bluetooth-plugin
```

**Step 3: Commit**

```bash
git add src/bluetooth/bt_pairing.cpp src/bluetooth/bt_plugin.cpp
git commit -m "feat(bluetooth): implement BlueZ device pairing"
```

---

### Task 15: Plugin RFCOMM Connect

**Files:**
- Create: `src/bluetooth/bt_rfcomm.cpp`

**Step 1: Implement RFCOMM connect**

`helix_bt_connect_rfcomm()`:
1. `socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM)`
2. Parse MAC string → `bdaddr_t` via `str2ba()`
3. Fill `sockaddr_rc` with MAC + channel
4. `connect()` with 10s timeout (set `SO_SNDTIMEO`)
5. Return fd on success, negative errno on failure

Include: `<bluetooth/bluetooth.h>`, `<bluetooth/rfcomm.h>`

`helix_bt_disconnect()` (for RFCOMM): just `close(handle)` — handle is the fd.

**Step 2: Build**

```bash
make bluetooth-plugin
```

**Step 3: Commit**

```bash
git add src/bluetooth/bt_rfcomm.cpp
git commit -m "feat(bluetooth): implement RFCOMM socket connect"
```

---

### Task 16: Plugin BLE GATT Connect & Write

**Files:**
- Create: `src/bluetooth/bt_ble.cpp`

**Step 1: Implement BLE GATT via D-Bus**

`helix_bt_connect_ble()`:
1. Find device path from MAC → `Device1.Connect()` via D-Bus
2. Wait for `ServicesResolved == true` (poll property, timeout 10s)
3. Enumerate `org.bluez.GattCharacteristic1` objects under device path
4. Find characteristic matching `write_uuid`
5. Try `AcquireWrite()` first (BlueZ 5.46+) → returns fd for direct writes (fastest)
6. Fallback: store characteristic D-Bus path for `WriteValue()` method calls
7. Store connection in internal table, return index as handle

`helix_bt_ble_write()`:
1. If have acquired fd: chunk data to MTU, `write()` to fd
2. Else: chunk to MTU, call `GattCharacteristic1.WriteValue()` per chunk via D-Bus
3. MTU from `Device1.Mtu` property (fallback 20)

`helix_bt_disconnect()` (for BLE): `Device1.Disconnect()` via D-Bus, close acquired fd if any, remove from table.

**Step 2: Build**

```bash
make bluetooth-plugin
```

**Step 3: Commit**

```bash
git add src/bluetooth/bt_ble.cpp
git commit -m "feat(bluetooth): implement BLE GATT connect and write"
```

---

## Phase 7: Integration & Testing

### Task 17: Integration Test — Full Stack

**Files:**
- No new files — manual testing on BT-capable device

**Step 1: Build everything**

```bash
make -j && make bluetooth-plugin
```

**Step 2: Deploy to test device with BT**

Copy `libhelix-bluetooth.so` next to the binary.

**Step 3: Test discovery**

1. Launch app: `./build/bin/helix-screen --test -vv`
2. Navigate to Settings → Label Printer
3. Verify "Bluetooth" appears in type dropdown
4. Select Bluetooth → verify scan button appears
5. Click Scan → verify nearby BT printers are discovered

**Step 4: Test pairing + print**

1. Select discovered printer
2. If unpaired: verify pairing modal appears, confirm pairing
3. Select label size, click Test Print
4. Verify label prints correctly

**Step 5: Test without BT hardware**

1. Remove `libhelix-bluetooth.so` or test on non-BT device
2. Verify "Bluetooth" option does NOT appear in type dropdown
3. Verify no errors in log, no memory allocated

---

### Task 18: Cross-Compilation Support

**Files:**
- Modify: `Makefile` (cross-compile bluetooth-plugin target)
- Modify: Docker cross-compile setup if needed

**Step 1: Add cross-compile support to Makefile**

Ensure the bluetooth-plugin target works with `CROSS_COMPILE=1` and `TARGET_TRIPLE=aarch64-linux-gnu`:
- Check for cross-compiled `libsystemd` and `libbluetooth` in sysroot
- Use correct pkg-config prefix

**Step 2: Build for Pi**

```bash
make bluetooth-plugin CROSS_COMPILE=1 TARGET_TRIPLE=aarch64-linux-gnu
```

**Step 3: Deploy and test on Pi**

```bash
PI_HOST=192.168.1.113 make deploy-pi
```

**Step 4: Commit**

```bash
git add Makefile
git commit -m "build: add cross-compilation support for bluetooth plugin"
```

---

## Phase 8: Translation Support

### Task 19: Add Translation Keys

**Files:**
- Modify: Translation YAML files
- Regenerate: `src/generated/lv_i18n_translations.c`, `.h`, `ui_xml/translations/translations.xml`

**Step 1: Add new translation keys**

Per [L064], add keys for:
- `"Bluetooth"` — type selector option
- `"Scan for Printers"` — scan button
- `"Bluetooth Printers"` — section header
- `"Pair Bluetooth Printer"` — pairing modal title
- `"Pair with %s?"` — pairing modal message

**Step 2: Regenerate translation artifacts**

```bash
make translations
```

**Step 3: Commit all generated files**

Per [L064]:
```bash
git add translations/ src/generated/lv_i18n_translations.c \
        src/generated/lv_i18n_translations.h \
        ui_xml/translations/translations.xml
git commit -m "chore(i18n): add Bluetooth label printer translation keys"
```

---

## Task Dependency Graph

```
Phase 1 (Protocol Extraction):
  Task 1 (Brother QL) ──┐
  Task 2 (Phomemo)    ──┤
                         ▼
Phase 2 (Plugin ABI):    │
  Task 3 (ABI header) ──┤
  Task 4 (Loader)     ──┤
                         ▼
Phase 3 (Settings):      │
  Task 5 (BT settings)──┤
                         ▼
Phase 4 (Backends):      │
  Task 6 (Brother BT) ──┤
  Task 7 (Phomemo BT) ──┤
  Task 8 (Wire up)     ─┤
                         ▼
Phase 5 (UI):            │
  Task 9 (XML)         ──┤
  Task 10 (Overlay C++)──┤
                         ▼
Phase 6 (Plugin .so):    │
  Task 11 (Build infra)──┤
  Task 12 (Core)       ──┤
  Task 13 (Discovery)  ──┤
  Task 14 (Pairing)    ──┤
  Task 15 (RFCOMM)     ──┤
  Task 16 (BLE GATT)   ──┤
                         ▼
Phase 7 (Integration):   │
  Task 17 (Full test)  ──┤
                         ▼
Phase 8 (Polish):        │
  Task 18 (Cross-compile)┤
  Task 19 (Translations) ┘
```

**Parallelizable pairs:**
- Task 1 + Task 2 (independent protocol extractions)
- Task 3 + Task 5 (ABI header + settings are independent)
- Task 6 + Task 7 (independent printer backends)
- Task 9 + Task 11 (XML layout + build infra are independent)
- Tasks 13–16 (plugin internals can be developed in parallel if different developers)
- Task 18 + Task 19 (cross-compile + translations are independent)
