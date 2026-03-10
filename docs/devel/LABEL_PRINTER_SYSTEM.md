# Label Printer System

HelixScreen supports printing spool labels to thermal label printers via three transports (USB, Network, Bluetooth) and three printer protocol families (Brother QL, Phomemo, Niimbot).

## Architecture Overview

```
┌──────────────────────────────┐
│     print_spool_label()      │  Entry point (label_printer_utils.cpp)
│  Selects transport + protocol│
└──────────┬───────────────────┘
           │
    ┌──────┴──────┬──────────────┬─────────────────┐
    │             │              │                  │
┌───┴────┐  ┌────┴─────┐  ┌────┴──────┐    ┌──────┴──────┐
│ Brother│  │ Phomemo  │  │ Phomemo   │    │  Niimbot    │
│ QL Net │  │ USB      │  │ BT (SPP/  │    │  BT (BLE)   │
│ (TCP)  │  │ (libusb) │  │  BLE)     │    │             │
└───┬────┘  └────┬─────┘  └────┬──────┘    └──────┬──────┘
    │            │              │                   │
    │  brother_ql_build_raster  │  phomemo_build_   │ niimbot_build_
    │            │              │  raster            │ print_job
    │            │              │                    │
    └────────────┴──────────────┴────────────────────┘
              ILabelPrinter interface
```

## Key Files

| File | Purpose |
|------|---------|
| `include/label_printer.h` | `ILabelPrinter` interface, `LabelSize`, `LabelPreset`, `PrintCallback` |
| `include/label_bitmap.h` | `LabelBitmap` — 1bpp monochrome bitmap (MSB first, black=1) |
| `src/system/label_printer_utils.cpp` | `print_spool_label()` — transport routing and printer dispatch |
| `src/system/label_renderer.cpp` | `LabelRenderer::render()` — renders spool info to LabelBitmap |
| `include/label_printer_settings.h` | `LabelPrinterSettingsManager` — persisted config |

### Protocol Implementations

| File | Protocol |
|------|----------|
| `include/brother_ql_protocol.h` | Brother QL ESC/P raster building |
| `include/phomemo_protocol.h` | Phomemo ESC/POS raster building |
| `include/niimbot_protocol.h` | Niimbot custom BLE packet building |
| `src/system/niimbot_protocol.cpp` | Niimbot packet framing, row encoding, print job builder |

### Transport Backends

| File | Transport |
|------|-----------|
| `src/system/brother_ql_printer.cpp` | Brother QL over TCP (network) |
| `src/system/phomemo_printer.cpp` | Phomemo over USB (libusb) |
| `src/system/brother_ql_bt_printer.cpp` | Brother QL over BT Classic (RFCOMM) |
| `src/system/phomemo_bt_printer.cpp` | Phomemo over BT Classic (RFCOMM) or BLE GATT |
| `src/system/niimbot_bt_printer.cpp` | Niimbot over BLE GATT |
| `include/bt_print_utils.h` | Shared RFCOMM send helper (Brother + Phomemo BT) |
| `include/bt_discovery_utils.h` | Brand detection table, BLE UUID matching |

### Bluetooth Plugin

| File | Purpose |
|------|---------|
| `include/bluetooth_plugin.h` | C ABI for runtime-loaded BT plugin |
| `include/bluetooth_loader.h` | `BluetoothLoader` singleton — dlopen wrapper |
| `src/bluetooth/bt_plugin.cpp` | Plugin core, sd-bus event loop |
| `src/bluetooth/bt_discovery.cpp` | BlueZ D-Bus device discovery |
| `src/bluetooth/bt_pairing.cpp` | BlueZ pairing, trust |
| `src/bluetooth/bt_rfcomm.cpp` | RFCOMM socket connect/write |
| `src/bluetooth/bt_ble.cpp` | BLE GATT connect/write via D-Bus |

## Printer Protocol Details

### Brother QL (ESC/P Raster)

- **Transport:** TCP (port 9100) or BT Classic RFCOMM
- **Protocol:** Binary ESC/P command stream
- **Sequence:** 200-byte invalidation → ESC @ init → raster mode → media info → auto-cut → per-row raster data → 0x1A print
- **Row format:** Horizontal flip, 90-byte row width for 62mm labels
- **DPI:** 300 (native)
- **Models:** QL-820NWB, QL-810W, QL-800, PT-*, TD-*, RJ-*

### Phomemo (ESC/POS Raster)

- **Transport:** USB (libusb), BT Classic RFCOMM, or BLE GATT
- **Protocol:** ESC/POS command stream with GS v 0 raster block
- **Sequence:** Speed + density + media type commands → GS v 0 raster → finalize + feed-to-gap
- **DPI:** 203 (native)
- **Models:** M110, M120, M02, Q199, and other M*/Q* series

### Niimbot (Custom BLE)

- **Transport:** BLE GATT only (Transparent UART service `e7810a71-...`)
- **Protocol:** Custom binary packets with XOR checksum
- **Packet format:** `[0x55 0x55 CMD LEN DATA... XOR_CHECKSUM 0xAA 0xAA]`
- **DPI:** 203 (native)
- **Print job sequence:**
  1. `SetDensity` (0x21) — density 1-5
  2. `SetLabelType` (0x23) — WithGaps/BlackMark/Continuous/Transparent
  3. `PrintStart` (0x01)
  4. `PageStart` (0x03)
  5. `SetPageSize` (0x13) — height + width as u16be
  6. Image rows: `PrintBitmapRow` (0x85) or `PrintEmptyRow` (0x84)
  7. `PageEnd` (0xE3)
  8. `PrintEnd` (0xF3)
- **Row encoding:** Per-row with 3-chunk or total black pixel count, repeat compression for identical consecutive rows
- **Models:** B21 (384px/48mm wide), D11/D110 (96px/12mm wide)

## Brand Detection

`bt_discovery_utils.h` contains a unified `PrinterBrand` table for detecting printer type from BLE device name:

```cpp
struct PrinterBrand {
    const char* prefix;
    bool is_ble;       // BLE-only (no SPP/RFCOMM)
    bool is_brother;   // Brother QL protocol
    bool is_niimbot;   // Niimbot protocol
};
```

Key helpers:
- `find_brand(name)` — table lookup by name prefix
- `is_brother_printer(name)` — Brother QL family
- `is_niimbot_printer(name)` — Niimbot family (B21, D11, D110)
- `name_suggests_ble(name)` — device needs BLE transport
- `is_label_printer_uuid(uuid)` — matches SPP, Phomemo BLE, or Niimbot BLE service UUIDs

## Bluetooth Transport

### Plugin Architecture

Bluetooth support is a runtime-loadable shared library (`libhelix-bluetooth.so`). Zero impact when BT hardware is absent — no libraries loaded, no threads started.

```
BluetoothLoader (dlopen) → libhelix-bluetooth.so (sd-bus, BlueZ D-Bus)
```

### RFCOMM (Brother, Phomemo SPP)

Shared `rfcomm_send()` helper in `bt_print_utils.cpp`:
1. Init BT context → connect RFCOMM → write data in chunks → 5s drain sleep → disconnect → deinit
2. Single shared mutex prevents concurrent RFCOMM connections
3. Detached thread, callback via `queue_update()`

### BLE GATT (Niimbot, Phomemo BLE)

Each backend manages its own BLE connection:
1. Init BT context → `connect_ble()` with service UUID → sequential `ble_write()` per packet → disconnect → deinit
2. Per-printer mutex serialization
3. Inter-packet delays: 10ms for image rows, 100ms for commands (Niimbot)

## Label Sizes

Each printer family defines its own label size table:

| Function | Printhead | DPI | Example Sizes |
|----------|-----------|-----|---------------|
| `BrotherQLPrinter::supported_sizes_static()` | 720px (62mm) | 300 | 29mm, 62mm, 29x90mm |
| `PhomemoPrinter::supported_sizes_static()` | varies | 203 | 40x30mm, 50x30mm |
| `niimbot_b21_sizes()` | 384px (48mm) | 203 | 50x30mm, 40x30mm, 50x50mm |
| `niimbot_d11_sizes()` | 96px (12mm) | 203 | 15x30mm, 12x40mm |
| `niimbot_sizes_for_model(name)` | auto-detect | 203 | Selects B21 or D11 from device name |

## Testing

| Test File | Coverage |
|-----------|----------|
| `tests/unit/test_niimbot_protocol.cpp` | Packet framing, checksum, print job sequence, blank rows, label sizes |
| `tests/unit/test_bt_discovery_utils.cpp` | UUID matching, brand detection, BLE classification |

## Adding a New Printer Protocol

1. Create `include/<brand>_protocol.h` with pure packet/raster building functions
2. Create `src/system/<brand>_protocol.cpp` with implementation
3. Create `include/<brand>_bt_printer.h` implementing `ILabelPrinter`
4. Create `src/system/<brand>_bt_printer.cpp` with BLE/RFCOMM transport
5. Add brand entries to `KNOWN_BRANDS[]` in `bt_discovery_utils.h`
6. Add routing in `label_printer_utils.cpp` (both size selection and print dispatch)
7. Add unit tests for protocol in `tests/unit/test_<brand>_protocol.cpp`
