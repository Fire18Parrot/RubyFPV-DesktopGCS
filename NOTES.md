# RubyFPV Desktop GCS — Defects, Investigations, and Retained Findings

Updated: 2026-10-24 (root cause of the black video established and fixed; see
STATUS.md §2.15). Branch: `refactor`. Preserve the `old` branch unchanged.

## Confirmed live test findings

### 1. RF reception is functional
- Evidence: central log records `Start receiving radio packets from VID 2172108829`; sustained `550-570 Video Stream 1 packets/sec`; telemetry stream also present.
- Status: live observed.

### 2. The video was received on the air and dropped by the pairing gate
- Evidence: `/tmp/ruby/rtr_loop_live/player.log` shows pipeline created and `80 frames decoded`, but the player reads from a limited initial chunk and then sees `The video stream pipe has no writer at this moment`.
- Root cause (established 2026-10-24) — **not** a player bridge defect: every
  radio interface of this desktop is receive-only (the in-kernel `rtw88`
  driver cannot inject raw RF frames, established from the disassembly of the
  loaded `rtw88_core.ko`), so the vehicle never received a Ruby pairing
  request and never paired (`bIsPairingDone = false` on the router, and the
  stock video gate of `processor_rx_video.cpp` drops every video packet of a
  vehicle that is not paired). The bridge symptoms were secondary.
- Fix: `isTxCapable = 0` for in-tree rtw88 drivers, radio TX opens refused on
  them, receive-only radio links stay open, and the router pairs the vehicle
  from its fresh ruby telemetry when no interface can send (a pairing
  fallback that needs no uplink; `code/r_station/pairing_telemetry.{h,cpp}`).
- Status: **OBSERVED LIVE 2026-10-25**: with the TX-capable RTL8812AU dongle
  the full app pairs (a real RF confirmation — no fallback needed) and
  renders the live video (user-confirmed). The telemetry fallback stays as
  the honest receive-only path when no TX interface exists.
- Severity: high (was the black video).

### 3. settings synchronization times out without protocol response
- Location: `code/r_central/handle_commands.cpp` `_commands_check_send_get_settings()`; `process_router_messages.cpp` command response gate.
- Root cause: the same missing uplink — the vehicle never received
  `GET_ALL_PARAMS_ZIP`, so there was nothing to answer with. The stock gate
  on `bPairedConfirmed` (before any command is sent) is upstream behavior
  and was restored; the earlier "first connection bypasses the pairing flag"
  gate was an invention that contradicted the upstream protocol and was
  removed together with the invented `MODEL_FIRMWARE_TYPE_OPENIPC`.
- What remains true: the vehicle type of the VTX in the test could not be
  proven to be a Ruby settings owner; the sync will be attempted only once a
  pairing exists, five attempts at most, and the user is then told the truth
  ("Vehicle settings were not provided by the vehicle") — no faked success.
- Status: **VALIDATED LIVE 2026-10-25** — the end-to-end sync completed
  against the real VTX once the pairing ran on a real (non-zero) controller
  id and an RF uplink existed (STATUS.md §2.16).
- Severity: high.

## Suspected defects (updated 2026-10-24)

- ~~Pairing remains pending because OpenIPC VTX does not send Ruby pairing
  confirmation~~ — superseded: the pairing stayed pending because the vehicle
  never received any pairing request (no uplink). The Ruby firmware type of
  the vehicle (11.8) is visible in its telemetry, and the pairing fallback
  accepts a Ruby vehicle on a receive-only controller.
- FC telemetry message is expected from a Ruby-capable VTX and will likely stay missing **while no uplink exists** (FC telemetry is requested by commands, and no command reaches the vehicle).
- Live player renderer may become black because any restart of the shared-memory/pipe stream causes EOF and GStreamer session exit; the restart guards of the earlier milestones (059f8df, 4cad4a4, 2ff7e87, ac2e1a0) address this and are retained.

## Recommended next actions

1. Run the ground station against the real VTX with the receive-only fixes:
   expect the TX-unavailable alarm, then pairing-by-telemetry, then video. No
   RF transmission is performed by this fix and none is required for it.
2. After the pairing is observed, check the settings sync behavior on the
   real vehicle (up to five honest attempts, then the truthful warning).
3. The durable uplink remains blocked by hardware (§4 of STATUS.md): an
   adapter whose driver supports the monitor vif (e.g. MT7612U) or the
   prepared out-of-tree driver still needs the user's approval.
4. Keep regression tests that distinguish software-only decode from live GCS rendering.

## Fixes committed on `refactor` since handoff
- `059f8df` stopped repeated vehicle-settings sync after five failed downloads.
- 2026-10-24: restored the upstream pairing/sync semantics (reverted the OpenIPC
  firmware type invention), modeled the receive-only rtw88 radios honestly
  (`isTxCapable = 0`, TX open refused, radio links stay alive in receive-only
  mode), added the pairing-from-telemetry fallback and the TX-unavailable
  alarm. Existing verification: `test_pairing_telemetry` 9/0,
  `test_radio_capabilities` 406/0 (including the live classification of
  `wlan1`), `test_video_rx_pipeline` 158/0, `test_menu_rf_profiles` 43/0,
  `test_config_persistence` 20/0, `test_sw_version_compat` 18/0.

## 2026-10-25 — the RTL8812AU era, and the desktop-port root causes closed
- The user attached an RTL8812AU dongle (out-of-tree aircrack driver,
  monitor+inject capable): the durable uplink exists; Stage-1 RX and
  Stage-2b TX passed on air; the full app paired with the real VTX,
  synchronized the vehicle settings and rendered the live camera feed
  (user-confirmed).
- The final blocking desktop defects and their fixes are documented in
  STATUS.md §2.16: the zero controller-ID stub on the desktop
  (`utils_controller.cpp` guard now includes `HW_PLATFORM_LINUX_GENERIC`),
  the `iw` capability failure (`cap_net_admin` on the caller does not
  propagate to the `iw` child → fixed by `setcap ... on /usr/bin/iw`), the
  router radio-count alarm loop, and the fuse-copy spawn path.
- Pending housekeeping: swap the deployed router binary for the clean build
  at the next app stop (`Text file busy` otherwise); the `/usr/bin/iw` file
  capability is a system-level setting and may need re-application after
  system updates.

## Additional verification
- These tests verify software parsing/persistence/pairing-decision behavior, not sustained live end-to-end video.