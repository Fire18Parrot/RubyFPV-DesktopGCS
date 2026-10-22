# RubyFPV Desktop GCS — Defects, Investigations, and Retained Findings

Updated: 2026-10-22 23:00 CEST. Branch: `refactor`. Preserve the `old` branch unchanged.

## Confirmed live test findings

### 1. RF reception is functional
- Evidence: central log records `Start receiving radio packets from VID 2172108829`; sustained `550-570 Video Stream 1 packets/sec`; telemetry stream also present.
- Status: live observed.

### 2. Desktop decoding starts but a persistent live frame has not been proven in GCS
- Evidence: `/tmp/ruby/rtr_loop_live/player.log` shows pipeline created and `80 frames decoded`, but the player reads from a limited initial chunk and then sees `The video stream pipe has no writer at this moment`, indicating the station/player bridge does not remain continuously fed under the observed configuration.
- Status: partially validated; sustained live rendering unproven in GCS.
- Severity: high.

### 3. settings synchronization times out without protocol response
- Location: `code/r_central/handle_commands.cpp:1987` request; `process_router_messages.cpp:1487` discards command responses unless `g_bFirstModelPairingDone`.
- Evidence: log shows `GET_ALL_PARAMS_ZIP` sent and repeatedly times out; zero `GET_ALL_PARAMS_ZIP response` records in `log_system.txt`.
- Cause assessment: first pairing can be completed for Ruby telemetry; command responses are then allowed. The VTX in the test appears OpenIPC-telemetry-only (`b-0`) and does not answer Ruby full-settings commands. We should not fake success.
- Status: confirmed architecture/protocol limitation; requires VTX firmware support or an alternative protocol.
- Severity: high.

## Suspected defects

- Pairing remains pending because OpenIPC VTX does not send Ruby pairing confirmation. Evidence: periodic pairing requests continue; `_check_update_first_pairing_done_if_needed` requires valid telemetry version and then handles first pairing as normal.
- FC telemetry message is expected from a Ruby-capable VTX and will likely stay missing on OpenIPC VTX.
- Live player renderer may become black because any restart of the shared-memory/pipe stream causes EOF and GStreamer session exit.

## Recommended next actions

1. Add a VTX firmware capability probe before requesting settings.
2. Add a mode that skips full Ruby settings sync for telemetry-only OpenIPC VTX and documents it.
3. Keep regression tests that distinguish software-only decode from live GCS rendering.
## Fixes committed on `refactor` since handoff
- `059f8df` stopped repeated vehicle-settings sync after five failed downloads.
  This reduces repeated pairing warnings on telemetry-only VTX firmware without
  claiming that settings synchronization succeeded.
- Existing verification: compatibility 18/0 and ambient capability 3/0.
  No live VTX settings or sustained-rendering validation is claimed here.

## Additional verification
- `test_video_rx_pipeline`: 158/0.
- `test_config_persistence`: 20/0.
- `test_sw_version_compat`: 18/0.
- `test_ambient_caps`: 3/0.
- These tests verify software parsing/persistence behavior, not sustained live end-to-end video.
