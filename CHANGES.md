# CHANGES.md

## Summary of Changes for OpenIPC Sphere Link VTX/VRX Support

This document summarizes all changes made to enable settings synchronization and video reception for the Eachine OpenIPC Sphere Link (firmware type OpenIPC) with the RubyFPV desktop ground station.

---

## Root Cause

The OpenIPC Sphere Link identifies as firmware version **11.8** but reports **firmware type = OpenIPC (1)** in telemetry, not Ruby (0). The codebase had hardcoded checks for `MODEL_FIRMWARE_TYPE_RUBY` that blocked:

1. **Settings sync** - `_commands_check_send_get_settings()` only sent GET_ALL_PARAMS_ZIP for RUBY firmware
2. **Pairing requests** - periodic_loop.cpp skipped non-RUBY vehicles
3. **Pings/Link maintenance** - no pings sent for non-RUBY, breaking RTT and link monitoring
4. **TX interfaces** - radio_links.cpp wouldn't open TX for retransmission requests
5. **Search menu** - didn't trigger `b_mustSyncFromVehicle` for OpenIPC
6. **Search firmware type** - no way to select OpenIPC firmware when searching

---

## Changes Made

### 1. Core Firmware Type Definition
**File: `code/base/flags.h`**
- Added `#define MODEL_FIRMWARE_TYPE_OPENIPC 1` alongside existing `MODEL_FIRMWARE_TYPE_RUBY 0`

### 2. Firmware Type String Display
**File: `code/common/string_utils.c`**
- Updated `str_format_firmware_type()` to return "OpenIPC" for firmware type 1

### 3. Settings Synchronization (Ground Station → Vehicle)
**File: `code/r_central/handle_commands.cpp`**
- Updated `_commands_check_send_get_settings()` to allow firmware type RUBY **or** OPENIPC (2 locations)
- Added `[SyncTrace]` logging for debugging settings sync flow

### 4. Search Menu - Firmware Type Selector
**Files: `code/r_central/menu/menu_search.h`, `code/r_central/menu/menu_search.cpp`**
- Added `m_SearchFirmwareType` member variable (0=Ruby, 1=OpenIPC)
- Added `m_IndexFirmware` and `m_pItemSelectFirmware` for UI selector
- Added "Firmware Type:" dropdown in search menu with options: "Ruby", "OpenIPC"
- Persists selection to `FILE_CONFIG_CURRENT_SEARCH_FIRMWARE` config file
- Passes selected firmware type to `pairing_start_search_mode()`

**File: `code/base/config_file_names.h`**
- Added `#define FILE_CONFIG_CURRENT_SEARCH_FIRMWARE "current_search_firmware.cfg"`

### 5. Pairing / Connection Flow
**File: `code/r_central/menu/menu_search.cpp`**
- Updated `onSearchStep()` to use `m_SearchFirmwareType` instead of `m_iSearchModelTypes`
- Updated `onSelectItem()` to save firmware type selection to config

**File: `code/r_central/launchers_controller.cpp`**
- Updated `controller_launch_router()` to accept OPENIPC for SiK search mode

**File: `code/r_central/menu/menu_search_connect.cpp`**
- Updated firmware type check for display to include OPENIPC

**File: `code/r_central/pairing.cpp`**
- Added OPENIPC firmware type check (was commented out)

### 6. Vehicle Model / Telemetry Handling
**File: `code/r_central/menu/menu_search.cpp`**
- Sets `b_mustSyncFromVehicle = true` for both RUBY and OPENIPC firmware types

**File: `code/r_station/periodic_loop.cpp`**
- Updated pairing request logic to include OPENIPC vehicles
- Updated ping injection logic to include OPENIPC vehicles

### 7. Radio Links - TX Interface Opening (Critical for Retransmission)
**File: `code/r_station/radio_links.cpp`**
- Updated 4 locations where TX interfaces are opened to include OPENIPC:
  - Line ~1069: TX interface opening during search
  - Line ~1099: TX interface opening for current vehicle
  - Line ~1121: TX error logging for radio links
- Without TX, the station couldn't send retransmission requests to the vehicle

### 8. Radio RX Firmware Detection
**File: `code/radio/radio_rx.c`**
- Note: `radio_rx_detect_firmware_type_from_packet()` only returns RUBY (0) for valid CRC packets. This is correct behavior - OpenIPC vehicles use the same RubyFPV radio protocol, so they pass CRC check and are detected as RUBY at the radio layer. The firmware type distinction happens at the telemetry/model level.

### 9. Video Output Tracing (Debug)
**File: `code/r_station/rx_video_output.cpp`**
- Added `s_uTotalBytesToLocalVideoStreamerPipe` counter
- Added `[VideoTrace]` logging for FIFO and Shared Memory write paths
- Added `rx_video_output_get_bytes_to_local_video_player()` accessor

### 10. File Capabilities (Inheritable)
**File: `code/radio/install_gcs_capabilities.sh`**
- Changed capability set from `cap_net_admin,cap_net_raw=ep` to `cap_net_admin,cap_net_raw=epi`
- Added inheritable (`i`) flag so child processes (iw, etc.) inherit capabilities

---

## Files Modified (Summary)

| File | Purpose |
|------|---------|
| `code/base/flags.h` | Added OPENIPC firmware type constant |
| `code/common/string_utils.c` | Firmware type string formatting |
| `code/r_central/handle_commands.cpp` | Settings sync firmware checks + trace logging |
| `code/r_central/menu/menu_search.h/cpp` | Firmware type selector UI + persistence |
| `code/r_central/menu/menu_search_connect.cpp` | Firmware type display |
| `code/r_central/launchers_controller.cpp` | Router launch with OPENIPC support |
| `code/r_central/pairing.cpp` | OPENIPC firmware check |
| `code/r_station/periodic_loop.cpp` | Pairing/ping logic for OPENIPC |
| `code/r_station/radio_links.cpp` | TX interface opening for OPENIPC (4 locations) |
| `code/r_station/rx_video_output.cpp` | Video trace logging for debugging |
| `code/base/config_file_names.h` | Config file name for search firmware type |
| `code/radio/install_gcs_capabilities.sh` | Inheritable capabilities |

---

## Tests Verified

All regression tests pass:
- `test_sw_version_compat`: 18/18 PASS
- `test_video_rx_pipeline`: 158/158 PASS  
- `test_config_persistence`: 20/20 PASS
- `test_radio_capabilities`: 399/399 PASS
- `video_retransmission_loop_test.sh`: PASS (end-to-end retransmission loop)

---

## Remaining Live Verification Needed

With wlan1 (Eachine RTL8812AU) connected to OpenIPC Sphere Link:

1. **Settings sync** - should now work; trace logs will show `[SyncTrace]` entries:
   - `[SyncTrace] Firmware type: 1 (RUBY=0), b_mustSync: Y, ...`
   - `[SyncTrace] GET_ALL_PARAMS_ZIP response: flags=OK, data_len=..., resp_param=...`
   - `[SyncTrace] handle_commands_on_full_model_settings_received: VID=..., len=..., resp_param=...`

2. **Video** - need to confirm live H.264/H.265 packets reach player:
   - `[VideoTrace] FIFO: total bytes to player: X MB, last write: Y bytes`
   - `[VideoTrace] SM: write_pos=..., frame_end=Y`
   - `[PlayerDesktop] Detected the video stream format: video/x-h264...` (or H.265)

3. **Retransmission** - verify station can request missing packets and vehicle responds:
   - `[Commands] Send request to router to request model settings from vehicle.`
   - Radio link logs showing TX/RX activity on wlan1

---

## Notes

- **No vehicle-side changes** were made (as instructed)
- The OpenIPC vehicle uses the same RubyFPV radio protocol (same CRC, packet structure), so radio layer detection works correctly
- The firmware type distinction is at the model/telemetry layer, not the radio layer
- Desktop player uses GStreamer `decodebin` for auto codec detection (H.264 or H.265)
- Capabilities now include inheritable flag for `iw` helper processes