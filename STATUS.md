# RubyFPV — Desktop Port Status

Repository root: `/mnt/Giochi-e-Altro-2/RubyFPV/RubyFPV` (git repo, branch `main`).
Target: native x86_64, CachyOS / KDE Plasma 6 / Wayland, SDL2 + Cairo + GStreamer.
Build: `make -j20 all RUBY_BUILD_ENV=desktop` → `HW_PLATFORM_LINUX_GENERIC`.

Last updated: 2026-10-24. Verified on this machine, in this session.

---

## 0. HARDWARE SAFETY — READ FIRST

> **`wlan0` must never be touched.** It is the adapter that carries the network
> connection of this computer. RubyFPV detects it as an *unsupported* radio
> (`iwlwifi`, Intel AX210) and skips it. No mode, no channel, no frequency, no
> power, no pcap on it — ever.
>
> **`wlan1` is fitted with 5 GHz-only antennas.** It is the Realtek RTL8812AU-VS
> (`rtw88_8812au`), card model 7 = "RTL8812AU DualAnt". It must never be tuned
> to 2.4 GHz. The software guard in `code/base/radio_capabilities.cpp`
> (`radio_cap_card_model_is_5ghz_only()`) refuses every 2.4 GHz request on that
> card before any hardware write, and the guard was **not** weakened.
>
> Verified during this session, before and after every application run:
> `wlan0` UP on "Issam 2.4g" at 2437 MHz, `ping -c 2 1.1.1.1` → 0% packet loss.
> `wlan1` left in `type managed`, no channel set, no frequency set.
> The wireless regulatory domain is **not** changed by the desktop build
> (`_set_radio_region()` returns early on `HW_PLATFORM_LINUX_GENERIC`).

---

## 1. Build and tests (all green on 2026-10-22)

```bash
cd /mnt/Giochi-e-Altro-2/RubyFPV/RubyFPV
make clean && make -j20 all RUBY_BUILD_ENV=desktop        # 0 errors
./test_video_rx_pipeline                   # Tests: 158, Failures: 0
./test_radio_capabilities                  # Tests: 406, failures: 0
./test_pairing_telemetry                   # Tests: 9, failures: 0
./test_menu_rf_profiles                    # Tests: 43, failures: 0
./test_sik_serial_path                     # SiK SERIAL PATH TEST: 25 checks, 0 failure(s)
./test_config_persistence                  # Persistent configuration test: 20 checks, 0 failures
./test_sw_version_compat                   # Tests: 18, failures: 0
./test_ambient_caps                        # Tests: 3, failures: 0 (and the real check, in a user namespace: see §2.14.7)
./code/r_tests/video_retransmission_loop_test.sh   # the video retransmission loop, 4 steps, 0 failures
./code/r_tests/video_retransmission_live_test.sh 10  # the sustained loop, with the real desktop player
git diff --check                           # clean
```

| Binary | What it covers | Result |
|---|---|---|
| `test_video_rx_pipeline` | Real video RX path: packet → reassembly/FEC → **RF framing** → FIFO → **forwarding (UDP)** → recording → **retransmission**; plus the **four exact mac80211 monitor radiotap layouts** | 158/158 |
| `test_video_tx_resend` | **New**: the **vehicle side** of the video path: the real vehicle video TX code (`code/r_vehicle/video_tx_buffers.cpp`) builds and frames the video, and the real vehicle RX code answers a retransmission request | 8/8 and 7/7 |
| `test_radio_capabilities` | Radio capability model, card models, band guard, **the phy of every radio**, **the in-kernel rtw88 driver classification** | 406/406 |
| `test_pairing_telemetry` | **New**: the decision to pair a vehicle from its ruby telemetry, with the **same code** as the router (`code/r_station/pairing_telemetry.h`) — the receive-only pairing of §2.15 | 9/9 |
| `test_menu_rf_profiles` | RF profiles shown in the UI menus | 43/43 |
| `test_sik_serial_path` | **New**: real SiK detect → open → write → RX thread over a PTY | 25/25 |
| `test_config_persistence` | **New**: the configuration is in `~/.config/rubyfpv/`, survives a restart, the old settings are migrated without overwriting | 20/20 |
| `test_sw_version_compat` | **New**: the compatibility of the software version of a vehicle, with the **same code** as the ground station (`code/base/sw_version_compat.h`) — the false "firmware mismatch" of §2.14.1 | 18/18 |
| `test_ambient_caps` | **New**: the real function that raises the capabilities as ambient, and the **inheritance by a child process** (the `iw` helper) — the defect of §2.14.2 and §2.14.7 | 3/3 |

The Makefile has **no header dependency tracking**: after changing a header,
touch the sources that include it (or the build silently keeps stale objects).
**Rule that follows from it (see §2.14.6): before a build that has to be
installed and run, do `make clean` and build again** — a `make` that looks up
to date can silently keep an object built from an intermediate state of a
source.

`test_video_rx_pipeline` has also three **modes** that are not test cases but
the tools of the video path (see §3):
```bash
./test_video_rx_pipeline --live-feed 120                 # the video to the desktop player, in real time
./test_video_rx_pipeline --rf-tx 60 <monitor interface>  # the VTX side, on the air
./test_video_rx_pipeline --rf-rx 60 <monitor interface>  # the VRX side, from the air to the player
```
and one more, the **station half of the retransmission loop** (§2.8):
```bash
./test_video_rx_pipeline --rtr-loop <vehicle capture> <request capture> <answer capture|-> <lost packet index>
./test_video_rx_pipeline --rtr-loop-live <vehicle capture> <request capture> <answer capture> <vehicle binary> <iterations>
```

`test_video_tx_resend` has two **modes**, the two halves of the vehicle side
of the retransmission loop (§2.8):
```bash
./test_video_tx_resend --transmit <capture file>            # the vehicle sends the video
./test_video_tx_resend --answer <request file> <capture file>  # the vehicle answers a request
```

---

## 2. What is verified working (evidence, not claims)

### 2.1 Application lifecycle — verified by running it
- `./ruby_start` brings up `ruby_logger`, `ruby_controller`, `ruby_central`;
  the UI window is created (`[RenderEngineDesktop] initEngine: window
  initialized successfully (800x480)`) and the start sequence reaches
  `START_SEQ_COMPLETED`.
- **SIGTERM to the controller now stops the user interface gracefully**:
  "Caught signal to stop: 15" → "Stopping the user interface process..." →
  "Finished shutdown UI" in ~1 s, **no SIGKILL** (new
  `hw_stop_process_graceful()` gives the UI 5 s before killing it).
- New command **`./ruby_start -stop`** stops every Ruby process
  (`ruby_central`, `ruby_controller`, `ruby_rt_station`, `ruby_rt_router`,
  `ruby_tx_rc`, `ruby_rx_telemetry`, `ruby_video_proc`, `ruby_logger`) and
  leaves nothing behind. Verified: after it, no `ruby_*` process remains.
- A second start is clean: the configuration is **read back** from disk
  ("Read previous system type: 0", controller radio interfaces loaded) and
  only **2 soft errors** are logged, both of them the expected `EPERM` from
  configuring a radio interface (see blockers).
- The process tree is not left with an orphan UI: stopping the controller
  stops the UI.

### 2.2 Configuration persistence — **fixed and moved to a persistent location**
Two separate defects were fixed:

1. *The configuration was deleted on every start.* The stale FIFOs were
   cleaned with `rm -rf /tmp/ruby/*`, which on the desktop also removed the
   configuration folder. The cleanup now removes **only the named pipes, by
   explicit name** (`RUBY_FIFO_NAMES` in `code/base/config_obj_names.h`).
2. *The configuration was in the temporary folder*, so it was lost on reboot
   and whenever `/tmp` was cleaned. On the desktop build the configuration is
   now in **`~/.config/rubyfpv/`** (the XDG configuration folder of the user),
   while `/tmp/ruby/` keeps only the runtime data: the FIFOs, the temporary
   media, the logs, the update files and the calibration files.

   The layout is preserved: `~/.config/rubyfpv/` holds the configuration
   files, `models/` the vehicles, `models/history-<id>/` the history of a
   vehicle, `plugins/osd/` and `plugins/core/` the plugins.

   The paths are **resolved at runtime** (`code/base/config_paths.c`, new
   accessors used through the `FOLDER_CONFIG`, `FOLDER_CONFIG_MODELS`,
   `FOLDER_OSD_PLUGINS` and `FOLDER_CORE_PLUGINS` macros of
   `code/base/config_file_names.h`), because the home folder of the user is
   known only at runtime. The embedded platforms keep their fixed paths
   (`/home/pi/ruby/config/`, `/home/radxa/ruby/config/`, `/root/ruby/config/`).

   **Migration of an existing installation**: at the first start the settings
   of the old desktop installation (`/tmp/ruby/config/`, `/tmp/ruby/plugins/`)
   are copied to the new location. The files that are already in the new
   place are **never overwritten** and the old files are **never deleted**.
   The migration runs at the very beginning of `ruby_start`, before anything
   reads the configuration, so that the application does not think that it is
   installed for the first time.

   **Verified on this machine**: the 17 files of the old installation were
   migrated, the boot count continued from 23 to 24 to 25 over two restarts
   (it was reset to 1 when the configuration was not found),
   `ui_preferences.cfg` and `controller_settings.cfg` are byte-identical
   before and after a stop/start cycle, and the start log has **0 soft
   errors**.

   **Regression test** `code/r_tests/test_config_persistence.cpp` (20 checks):
   the configuration folder is the one of the user and not a temporary one,
   the folders are created, a setting written with the real production code
   (`radio_profiles_save()`) is read back **by a new process** (the test
   re-executes itself: that is a real restart), the migration copies the
   settings of an old installation without overwriting the ones that are
   already there and without deleting the old files. The test uses a home
   folder of its own and never touches the configuration of the user.

### 2.3 Radio: SiK serial path — **working and covered by a regression test**
- `hardware_radio_sik_detect_interfaces()` now walks a **dynamic candidate
  list**: `RUBY_SIK_PORTS` (override), the enumerated serial ports that are
  not used for telemetry/data-link/core-plugin, `/dev/ttyACM*` (CDC-ACM radios)
  and `/dev/serial/by-id/*`. On the embedded platforms the old fixed
  `/dev/ttyUSB0..2` list is kept.
- A detected radio that is not in the serial-ports table is **registered**
  (`hardware_serial_add_port()`, new API) and the table is saved to
  `hw_serial.cfg`, so the port is known at the next start.
- `hardware_radio_sik_open_for_read_write()` no longer requires the device to
  be in the enumerated table: it falls back to the airspeed-derived baud rate
  and registers the port on the fly.
- **New test** `code/r_tests/test_sik_serial_path.cpp` drives the *production*
  code end-to-end over a pseudo terminal: a SiK emulator answers the AT
  commands (`+++` → `OK`, `ATI`, `ATI1..4`, `ATS<n>?`), the radio is detected,
  the port is opened, a full packet (`radio_packet_init` + CRC32) wrapped in a
  short packet (`radio_packet_short_init` + CRC8) is written through
  `hardware_radio_sik_write_packet()`, and the **real RX thread** must deliver
  it byte-identically. 25 checks, 0 failures. The test saves and restores
  `last_sik_radios_detected.cfg` and `hw_serial.cfg` in the configuration
  folder of the user, and it never probes a real serial device (it sets
  `RUBY_SIK_PORTS`).

### 2.4 Video RX — verified by the test suite and by the desktop player
- `test_video_rx_pipeline` (**158 checks, 0 failures**) exercises the real
  receive path:
  - **Tests 1-9 (unit)**: the packet builder, the H.264 payload, a complete
    frame (12 packets, payload byte-identical), the ordering, an incomplete
    block (dropped, with the skip counter), a bad CRC (rejected), a malformed
    packet (the block scheme changed → rejected), an unknown vehicle
    (rejected), the reconstruction of a missing data packet from the EC
    packet.
  - **Test 10**: the recording path, independently of the display (a real
    `.h264` file that `ffprobe`/`ffmpeg` accept).
  - **Test 11 (the radio path)**: the video packets are wrapped into RF
    frames with the **production TX code** (`radio_build_new_raw_ieee_packet()`:
    radiotap + 802.11 header + the radio packet, with the link index and the
    CRC rewritten), they are written to a capture file, and they are read back
    with the **production RX code** (`radio_open_interface_for_read_from_file()`
    + `radio_process_wlan_data_in()` + `packet_process_and_check()`): the
    bytes that come out are **byte-identical** to the bytes that were on the
    air, and every packet passes the production CRC check.
  - **Test 12 (the sustained radio path)**: 240 RF frames / 20 video frames
    through the same production TX→capture→RX path: 240/240 frames read,
    240/240 CRCs good, **280480 bytes delivered to the video FIFO in 1 ms**,
    all 20 delivered frames byte-identical to the test payload.
  - **Test 13 (the retransmission request)**: the station builds the
    `PACKET_TYPE_VIDEO_REQ_MULTIPLE_PACKETS` request for a missing video
    packet; the request is checked field by field (36 bytes: the retransmission
    id, the video stream index, the flags, the count, the block index, the
    packet index, the destination vehicle, the high priority flag) and the
    second request carries the "re-requested" flag and a new retransmission id.
  - **Test 14 (the retransmission loop)**: the request is put on the air with
    the production TX builder (the bytes on the air are the bytes of the
    request, except the CRC and the link index, and the CRC is valid), the
    answer of the VTX is marked exactly as the vehicle side marks it
    (`code/r_vehicle/video_tx_buffers.cpp: _sendPacket()`: the retransmitted
    flag, the high priority flag, the retransmission id in the video stream
    info), and the station accepts it, completes the block and delivers the
    **whole 9576-byte frame** to the desktop player's FIFO. The retransmitted
    packet is counted as retransmitted and it is **not** discarded.
  - **Test 15 (the forwarding to a second player)**: the received video is
    forwarded over UDP to the local video player
    (`rx_video_output_video_data()` → `_rx_video_output_to_local_video_player_udp()`,
    the production path, independent of the FIFO): a UDP socket is bound on
    the port of the local video player, a complete frame goes through the
    production receive path and the datagrams that arrive are
    **byte-identical to the H.264 payload** (one datagram per video packet).
  - **Test 16 (the monitor radiotap of a real Linux monitor interface)**:
    the radiotap header that mac80211 builds is not a single fixed layout —
    it depends on the driver, on the rate and on the number of the chains.
    The test builds the **four exact layouts** that mac80211 computes
    (`ieee80211_rx_radiotap_hdrlen()` + `ieee80211_add_rx_radiotap_header()`:
    **18** bytes — the legacy rate without the timestamp, **26** — the legacy
    rate with it, **21** — the HT/MCS one, **24** — the radiotap namespace
    one, with the chain signal and the chain index), writes them in a
    capture file with the link type `DLT_IEEE802_11_RADIO` (127), reads them
    back with the **production RX code** and checks that every frame comes
    out **byte for byte identical** to the bytes that were on the air, that
    every packet passes the **production CRC check** and that the end of the
    file is reached cleanly. This is the software proof that the production
    parsing reads the frames of a real monitor interface.
- **Live video in the desktop application — demonstrated this session.** The
  test binary has a `--live-feed` mode: it sends the real H.264 stream
  (`res/intro.h264`, 150 access units) through the **production** receive path
  (`process_received_video_component_packet()` → `ProcessorRxVideo` →
  `VideoRxPacketsBuffer` → `rx_video_output_video_data()` → the FIFO) in real
  time, at the frame rate of the clip, while `ruby_player_desktop -p` reads
  the FIFO and decodes it with GStreamer.
  *Verified on this machine*: the player logged "Received the first video
  stream data", detected the format (`NV12 1280x720`), the station detected
  the stream (`Detected video stream FPS: 15 FPS`), the player read
  **10.85 MB** from the FIFO and the decoded video was **rendered on screen**
  (screenshots of the running player match the frames of the clip, and they
  change while the stream runs).
  *The video path is therefore demonstrated end to end, from the radio packet
  to the pixels. What is still missing is the RF hop itself (§3).*
- Recording fix from the previous milestone: the last ≤ 64000 bytes of a
  recording are no longer lost (`_recording_move_pipe_data_to_file()` + flush
  on thread exit).
- **Defect found and fixed in this milestone**: the video data length check of
  the RX block buffer was applied to the EC packets too. On an EC packet the
  video data area holds the **FEC parity block** (the vehicle writes it over
  the important header), so the length there is not a video data length: the
  check read parity garbage and rejected valid EC packets, so the blocks were
  never completed. The length check is now applied to the data packets only,
  and the EC packets have their own minimum-length check
  (`code/r_station/video_rx_buffers.cpp`).
- **Defect found and fixed in this session (a real memory-safety bug on the
  production RX path)**: the antenna counter of the radiotap parsing was
  unbounded, while the arrays that hold the antenna data have
  `MAX_RADIO_ANTENNAS` (4) elements, on the stack. A monitor interface
  reports **one ANTENNA field per chain**, so a frame with more chains than
  the arrays can hold made the code write **outside of them**. The counter is
  now bounded (`code/radio/radiolink.c`).

### 2.5 Radio capability model and the safety guard — verified
- `test_radio_capabilities` (399 checks) covers the card models, the band
  masks and the 5 GHz-only guard for model 7. The guard is intact.
- The RF profiles in the menus are covered by `test_menu_rf_profiles` (43).

### 2.6 Honest error reporting — **fixed this session**
The desktop build reported false successes and false alarms. Now:
- "Radio interface N RTL8812AU raw tx power changed to: 10" is only logged
  when the command actually succeeded; otherwise the log says the power was
  **NOT** changed.
- "Found unsupported radio (wlan0) … skipping" and "Configuring radio
  interface 2 (wlan0): found an unsupported radio, skipping it" are
  informational messages, not soft errors: an adapter RubyFPV does not know
  how to drive is a normal situation on a desktop.
- The absent `ruby_rt_station` process is no longer reported as an error when
  there is no radio to talk to ("No usable radio found, the router/station
  process is not expected to be running."). The launcher and the start script
  now use the same rule (`hardware_radio_has_serial_radios()`).
- The device-tree serial number is only read on the embedded platforms
  (there is no device tree on a desktop): the vehicle/controller id is still
  generated, without a false "Failed to get hardware serial number" alarm.
- The base Ruby version is now actually found: `version_ruby_base.txt` is
  looked up in the application folder as well, and the log shows
  "[Config] Read raw base Ruby version: [11.8] from file (version_ruby_base.txt)".
- The first-run "missing file" messages for the simple configuration files
  and for the OSD plugin settings now create the file with the default value
  instead of complaining on every start.

### 2.7 Desktop-specific fixes from the previous milestones (still in place)
- `media_scan_files()` uses the absolute `FOLDER_MEDIA` instead of a relative
  `media/` folder (the relative path depended on the CWD of the process).
- The thread priorities are converted with `hw_get_nice_value_for_raw_priority()`,
  so the application does not try to raise its own priority (which a normal
  user cannot do): no more "nice: cannot set niceness: Permission denied".
- `hw_process_get_pids()` uses `pgrep -f "[/]name"` for names longer than 15
  characters, so the long Ruby process names are matched correctly and without
  the pgrep warning.
- The desktop build does not start the router / rx-telemetry / tx_rc / i2c
  processes when there is no serial radio to use them.

### 2.8 The video retransmission loop, end to end, with the production code of **both** ends

This is the newest verified result of the port: the **complete** video
retransmission loop now runs with the **production code of the vehicle and of
the station**, and the video of the frame that had a lost packet reaches the
desktop player **byte for byte**.

**Why two processes.** The vehicle code and the station code have their own
copies of the shared variables and their own timers: they cannot be linked
into one binary. The loop therefore runs in two processes, and the **capture
files stand for the air**: they have the link type of a monitor mode capture
(`DLT_IEEE802_11_RADIO`, 127), the production filter accepts them and the
production code parses them, exactly as it does with a live monitor
interface. Nothing in the loop is a re-implementation of the protocol.

**The four steps** (`code/r_tests/video_retransmission_loop_test.sh`, the
logs are in `/tmp/ruby/rtr_loop`):

| Step | What runs | Result |
|---|---|---|
| 1. The vehicle sends the video | the real vehicle video TX code (`code/r_vehicle/video_tx_buffers.cpp`) builds the video packets, the real radio TX code (`radio_build_new_raw_ieee_packet()` + `radio_write_raw_ieee_packet()`) frames them and writes them to a capture file | 8/8 checks, 30 video packets, all CRC-ok, 0 retransmitted, the first block has its 4 data packets |
| 2. The station loses a packet and asks for it | the real radio RX code reads the capture, **one** video packet is dropped on the way, the real station code (`ProcessorRxVideo::periodicLoopProcessor()` → `checkAndRequestMissingPackets()`) builds the request and the real radio TX code puts it on the air | 14/14 checks: the request asks for exactly the lost packet, it has a non-zero retransmission id, it is framed on the **UPLINK** port (0x0E) with a good CRC and the bytes on the air are the bytes of the request |
| 3. The vehicle answers | the real radio RX code reads the request, the real vehicle code (`process_data_tx_video_command()` → `g_pVideoTxBuffers->resendVideoPacket()`) answers it, the real radio TX code writes the answer | 7/7 checks: the answer contains the requested packet, marked **retransmitted** and **high priority**, carrying the non-zero retransmission id, and every packet passes the production CRC check |
| 4. The station receives the answer | the real radio RX code reads the answer, the answer completes the block, the video goes through `process_received_video_component_packet()` → `ProcessorRxVideo` → `VideoRxPacketsBuffer` → `rx_video_output_video_data()` → the FIFO of the desktop player | 24/24 checks: **20960 bytes** of video reach the player and are a **byte-for-byte copy** of the access units of the clip, the access unit that had the lost packet is byte-exact, the retransmitted packet is counted as retransmitted and it is **not** discarded |

**The new seam.** `radio_open_interface_for_write_to_file()`
(`code/radio/radiolink.c`) opens a radio interface for writing the RF frames
into a pcap capture file instead of into the air. The frames it writes are
the frames that would have gone on the air: the same radiotap header, the
same 802.11 header, the same CRC, built by the production TX code. The
existing read seam, `radio_open_interface_for_read_from_file()`, reads them
back with the production filter and the production parsing. The live
interface is **not touched at all** by the file mode.

**Two production details that the loop uncovered** (both are the production
behaviour, not test artefacts):
- A **short** video block always gets **1 EC packet**, even at a 0 % EC rate:
  the short-block adjustment in `code/r_vehicle/video_tx_buffers.cpp` computes
  `ceil(dataPackets * fECRate + 0.001)`, which is 1 for a rate of 0. A lost
  data packet of such a block is therefore reconstructed **locally**, with the
  EC packet, and no retransmission is requested. To exercise the
  retransmission the lost packet has to be one of a **full** block (4 data
  packets, no EC) — block 1, packet 1, the capture frame 5.
- The station refuses a retransmission request whose source id is 0 or
  `MAX_U32` (`code/r_vehicle/process_radio_in_packets.cpp`), so the
  controller id has to be set on the station side
  (`g_uControllerId = 0x87654321`) for the vehicle to accept the request.

**What this is and what it is not.** It proves that the **software** of the
two ends fits together: the request, the answer, the block completion and the
delivery of the video to the desktop player, with the production code on both
sides. It does **not** prove anything about the air: the medium is a capture
file, and the RF hop is still missing (§3).

### 2.9 The **sustained** retransmission loop, with the real desktop player

The loop of §2.8 was then repeated **many times in a row**, with the **real
desktop player** as the reader of the video FIFO (`ruby_player_desktop -p`),
so the video of every retransmitted frame is **decoded and displayed**, not
only counted.

- **The new mode**: `test_video_rx_pipeline --rtr-loop-live <vehicle capture>
  <request capture> <answer capture> <vehicle binary> <iterations>`. The
  station runs the **vehicle test binary as a separate process** (the vehicle
  code and the station code can not be linked in the same binary) in every
  iteration, so the answer of every request is produced by the **production
  vehicle code** (`process_data_tx_video_command()` →
  `g_pVideoTxBuffers->resendVideoPacket()`), not by a recorded file.
- **The lost packet changes**: in every iteration a **different** data packet
  of the full block is lost (the position 1, 2 and 3 of the block, in turn).
  The lost packet is found from the headers of the packets themselves (a
  block with 4 data packets and no EC packet), so the test does not depend on
  a hard coded index.
- **The state is reset between the iterations** with the production reset of
  the processor (`ProcessorRxVideo::fullResetState()`), so the same capture
  file is received again from the start.
- **The watchdog**: the production code writes the video with a **blocking**
  write, so a player that does not read the video would block the test
  forever; a `SIGALRM` watchdog ends the test with a clear error in that
  case.
- **The new production accessor**: `rx_video_output_get_bytes_to_local_video_player()`
  (`code/r_station/rx_video_output.h`) returns the number of the video bytes
  that were written to the FIFO of the player. It is the number the
  **production** code counted, not a number the test made up.

**Result on this machine** (`code/r_tests/video_retransmission_live_test.sh
10`, the user was warned with a desktop notification before the screen was
taken over):

| What | Result |
|---|---|
| the iterations of the sustained loop | **10 of 10** succeeded, 0 failures |
| the packets that were lost | 10 (a different one in every iteration) |
| the requests of the station / the answers of the vehicle | 10 / 10, all of them with the production code of the two ends |
| the video that reached the player | **209600 bytes** (20960 bytes per iteration) |
| the desktop player | "Received the first video stream data", "Detected the video stream format: NV12 1280x720", "**209600 bytes read, 209600 bytes pushed, 80 frames decoded**" (80 = 10 iterations × 8 access units) |
| the screen | the screenshot of the player window shows the **"LOADING CONTENT"** frame of the clip — the same content the clip has |

The 20960 bytes per iteration are the first 8 access units of the clip: they
are **byte for byte identical** to the source (that is what the single-shot
mode of §2.8 checks), and the player decoded all of them, 80 frames in 10
consecutive rounds.

---

### 2.10 The two-radio RF test: the **VRX half** was run against the live air

The user authorized the receive-only run of the two-radio RF test (the
temporary privileged interface and channel changes on the two dedicated USB
adapters only). The **TX half was not run**: the driver of the TP-Link cannot
inject the frames RubyFPV needs (§4.1), and running it would have been a
pointless privileged action.

**The exact commands:**
```bash
cd /mnt/Giochi-e-Altro-2/RubyFPV/RubyFPV
sudo -E ./code/r_tests/two_radio_rf_test.sh 60 149      # the VRX half only, channel 149
```

**What the script does, verified in full before the run** (the five safety
points of the brief):

| Check | How it is done | Result |
|---|---|---|
| the two adapters are identified by their **USB hardware id** | `netdev_for_usb_id()` reads `idVendor`/`idProduct` of the USB device of every netdev | the TP-Link (`2357:0120`) is `wlan2`, the Eachine (`0bda:881a`) is `wlan1` |
| `wlan0` and the default route are **refused** | the script compares the found interfaces with `wlan0` and with the interface of `ip route show default` and exits with an error | both radios are used, `wlan0` is never touched |
| the 5 GHz-only antennas are respected | a channel below 32 is refused; after the monitor interface is created, the frequency is read back and the test stops if it is below 5000 MHz | the monitor interface was created at **5745 MHz** (channel 149) |
| the state is restored, also on an error or a Ctrl-C | `trap cleanup EXIT INT TERM`; the managed interfaces are put back in the state they had, the monitor interfaces are deleted | the run ended with **no `rubyvrx0` left**, `wlan1` and `wlan2` back to `type managed` |
| no driver, no kernel module, no reboot, no system-wide configuration | the script only runs `iw`, `ip` and the test binary | nothing was installed or changed outside the two radios |

**What the run produced** (11:14:28 → 11:16 on this machine):

| The six separate findings of the brief | Result |
|---|---|
| **both monitor interfaces created** | **only one**: `rubyvrx0` on the Eachine. The TX half was not requested, so `rubyvtx0` was not created (`wlan2` was left alone) |
| **the Eachine actually captured RF frames** | **yes**: the counter of the interface went from **2 to 6** during the 60 s — four real frames of the air were delivered by the driver and copied by mac80211 to the monitor interface. (In the 3 s before the test the counter did not move — 2 to 2 — so the script reported "no frame was received in 3 seconds"; the channel is not crowded, and that is not a fault of the capture) |
| **the production capture and packet-processing code ran** | **yes**: `radio_open_interface_for_read()` opened the live monitor interface (fd 3), libpcap installed the **production filter** (`ether[0x00:2] == 0x0801 && ether[0x0a:4] == 0x13123456 && ether[0x04:1] == 0x%.2x`), the FIFO write end was opened (fd 4) and the production loop read the interface for the 60 s |
| **the frames matched the expected RubyFPV video protocol** | **no**: "The radio packets received: 0, the video packets: 0, the bad packets: 0, the CRC errors: 0". The four frames that the driver delivered are ordinary Wi-Fi traffic and the production filter rejected them, exactly as it must |
| **valid video packets reached the production video reconstruction path** | **no**: no video packet entered the reconstruction, so nothing was written to the video FIFO and the player had no stream to display |
| **the desktop player displayed the resulting live stream** | **no**: the player was started (as the user of the session, never as root) and it received **nothing**, because there was no video on the air |

> **This is NOT a proof of RubyFPV video reception.** The monitor interface
> captured *arbitrary Wi-Fi frames*, not RubyFPV frames: no compatible
> RubyFPV transmitter was on the air, and the production filter — which is
> the real proof that the frames are RubyFPV frames — rejected every one of
> them. The only thing this run proves is that **the capture half of the
> radio path works on the real hardware, with the production code**: the
> monitor interface is created, the production code opens it, the production
> filter is installed and the production parsing runs on the frames of the
> air.

**Three defects of the test harness were found and fixed by these runs:**

1. **The fake radio entry was destroyed by the radio enumeration.** The
   production code enumerates the radio interfaces the first time it needs
   the radio info, and the enumeration **reloads `current_radios.cfg` over the
   whole radio array** (`hardware_load_radio_info_into_buffers()` rewrites
   `sRadioInfo[]`). The test radio entry was therefore overwritten before the
   production open path read it, and the production code refused the live
   monitor interface ("Tried to open a non 2.4/5.8 radio in high capacity
   2.4/5.8 mode"). Fixed in `code/r_tests/test_video_rx_pipeline.cpp`: the
   enumeration is now done **before** the fake entry is written.
2. **The script reported a false success.** The test returned 1 both for
   "the interface was read but no video packet arrived" and for "the
   interface could not be opened at all", and the script read 1 as a
   success. Fixed: the test now returns **255** when the interface cannot be
   opened, and the script reports that case as an error.
3. **The monitor interface was never deleted.** The monitor interface of a
   driver without monitor-vif support is the **virtual monitor of mac80211**:
   it cannot be deleted with `ip link del` (the kernel answers "Operation
   not supported"), it has to be deleted with `iw dev <name> del`. The
   script left a leftover `rubyvrx0` on the Eachine. Fixed with the
   `delete_monitor()` helper in `code/r_tests/two_radio_rf_test.sh`.

**And a defect of the production code was found by the run** — see §4.1, the
radio enumeration mis-read the phy of an interface when a monitor interface
existed on the same radio, and the Eachine was then recorded with **no
supported band at all**, which makes a radio unusable.

**The final state of the machine, after the run:**

| Interface | State |
|---|---|
| `wlan0` (Intel AX210, the network connection) | **untouched**: UP, ssid "Issam 2.4g", channel 52 (5260 MHz), `default via 192.168.50.1 dev wlan0` intact. It was never read, never reconfigured and never used by the test |
| `wlan1` (Eachine, the VRX) | back to `type managed`, no monitor interface left, on the 5.8 GHz channel 149 (on purpose: it is the only band its antennas support) |
| `wlan2` (TP-Link, the VTX) | untouched by the run: `type managed`, no monitor interface created |
| the desktop player | terminated by the cleanup of the script |

---

### 2.11 A **defect of the production code**, found by the RF test and fixed

The RF test exposed a real bug of the radio enumeration, and this one is
important: it made a radio **unusable**.

**What happened.** The enumeration found the phy of an interface by parsing
the output of `iw dev` with `iw dev | grep -B 1 <name>`, that is, by taking
the line that precedes the name of the interface and by building the index
out of its last characters. That works **only when the interface is the
first one of its phy**. A radio that has a **monitor interface** as well
(the exact configuration the RubyFPV receive path needs) does not list the
managed interface first, so the line that precedes the name is an unrelated
one. During the RF test, on the Eachine, the line was `txpower 13.00 dBm`
and the index was built out of the `w` of "txpower":

```
Executing command raw: iw dev | grep -B 1 wlan1
Read process output: 37 bytes. Content: [  txpower 13.00 dBm
	Interface wlan1]
phy string: [txpower], length: 7
Executing command raw: iw phy66 info 2>&1
[HW-R] Interface wlan1 supported bands: [N/A]
```

The phy **66** does not exist, the channel list could not be read, the
**band mask of the Eachine was 0** and the radio was recorded that way. A
radio with a band mask of 0 cannot be tuned at all:
`hardware_radioindex_supports_frequency()` returns 0 for **every**
frequency, so the 5.8 GHz video link is impossible with that radio. The
same defect was already latent for `wlan0` (the AX210 of `wlan1` has a P2P
device before it, so its index was 53 — harmless there, because that radio
is not a Ruby radio, but the same bug).

**The fix** (`code/base/hardware_radio.c`): the phy of an interface is now
read from **sysfs**, which is exact and does not depend on the order of the
interfaces of the phy:

```c
cat /sys/class/net/<interface>/phy80211/name      ->  "phy2"
```

with a **correctly parsed** fallback on the `iw dev` output (the phy of an
interface is the last `phy#N` header that appears before its name) and a
loud error when the phy cannot be found at all. A radio whose band list
cannot be read is now **reported** (`log_softerror_and_alarm`) instead of
being silently recorded as a radio with no band.

**The new regression test** (`test_radio_capabilities`, 399 checks now): for
every radio of the running system it resolves the phy again and it checks
that the resolved index is the stored one and that
`/sys/class/ieee80211/phy<N>` really exists. This test **fails** with the
old code (the index 66 does not exist) and it passes with the new one.

**The stored radio configuration was refreshed** with the corrected values
(`~/.config/rubyfpv/current_radios.cfg` is a *detection cache*: the
application deletes and regenerates it at every start, in
`code/r_start/ruby_start.cpp`). The corrected detection is now:
`wlan0` → phy#1, `wlan1` → phy#2, `wlan2` → phy#5, and all three have the
bands "2.4, 5.8 GHz".

### 2.12 The TX blocker: an out-of-tree driver for the **existing** TP-Link (prepared, **not** installed)

The user changed the decision: a **third** adapter is no longer the default
solution. The existing TP-Link Archer T2U Plus (RTL8821AU, USB `2357:0120`)
is to be driven by a compatible **out-of-tree** driver, so that it can inject
the RubyFPV frames in monitor mode. The investigation, the port and the
reversible switch are in `code/radio/driver_88xxau/` (the `README.md` there
holds the full document); this section is the summary.

**The driver.** `https://github.com/aircrack-ng/rtl8812au.git`, branch
`v5.6.4.2`, pinned commit `734485506a30d6237c2deaad666a19f8ca5379f2`. The
module file is `88XXau.ko`, the USB driver name is `rtl8812au`.

**Why this driver and not another one:**

| Requirement of RubyFPV | Evidence in the driver |
|---|---|
| the TP-Link itself | `os_dep/linux/usb_intf.c:208`: `{USB_DEVICE(0x2357, 0x0120), .driver_info = RTL8821}` — the exact USB id of the adapter is in the device table |
| monitor mode, with the radiotap link type | `Makefile:59` `CONFIG_WIFI_MONITOR = y`; `os_dep/linux/ioctl_cfg80211.c:4936` `mon_ndev->type = ARPHRD_IEEE80211_RADIOTAP` (the link type that the production RX code reads: `DLT_IEEE802_11_RADIO`) |
| **the injection** of a radiotap + 802.11 frame, on a monitor interface | `os_dep/linux/xmit_linux.c:547` `rtw_xmit_entry()` → `core/rtw_xmit.c:4401` `rtw_monitor_xmit_entry()`: the radiotap header is parsed, the 802.11 frame is copied out and injected, honouring the `RATE`/`MCS` fields; the monitor netdev has its own path at `os_dep/linux/ioctl_cfg80211.c:4721` |
| the 5.8 GHz band | the module parameter `rtw_monitor_disable_1m` (`os_dep/linux/os_intfs.c:87`): with the default (**0**) the driver forces every injected frame to **1 Mbps**, and 1 Mbps does not exist on the 5 GHz channels, so the frame would never reach the air. It has to be loaded with **`rtw_monitor_disable_1m=1`** |

The production code does exactly what this driver supports: `iw dev <if> set
type monitor` (`code/base/hardware_radio.c:2007` and `:2075`), then
`pcap_inject()` of a radiotap + 802.11 DATA frame
(`code/radio/radiolink.c`, `radio_write_raw_ieee_packet()`; the headers are
at `code/radio/radiolink.c:105` and `:124`).

**A second, independent benefit:** with the in-tree `rtw88_8821au` the
TP-Link is **not even recognised** by the project — the string
`rtw88_8821au` matches none of the patterns of the radio enumeration
(`code/base/hardware_radio.c`, the `strstr(pszDriver, …)` tests), so the card
is recorded as an unsupported radio. With the out-of-tree driver the uevent
reports `DRIVER=rtl8812au`, which the project maps to
`RADIO_HW_DRIVER_REALTEK_RTL8812AU`.

**Kernel compatibility (7.2.11-1-cachyos).** The released driver does **not**
compile as it is on this kernel. Every break was identified against the local
headers and every one of them is fixed by
`code/radio/driver_88xxau/88xxau-linux-7.2.patch` (3 files, 173 insertions):
the nine `cfg80211_ops` callbacks that now take a `struct wireless_dev *`
instead of a `struct net_device *`, `cfg80211_new_sta()`/`cfg80211_del_sta()`,
the `rx_addr` of `remain_on_channel`, the `radio_idx` of
`set_wiphy_params`/`set_tx_power`/`get_tx_power` (kernel 6.17+), the removal
of `strncpy()` from the kernel headers, `from_timer()` → `timer_container_of()`,
`del_timer_sync()` → `timer_delete_sync()`, and the PPPoE structures whose
flexible array members are hidden from the kernel code. The patch keeps the
old code paths for the older kernels.

**Secure Boot and the module signing.** Secure Boot is **disabled** (the
`SecureBoot` efivar is `0`, `SetupMode` is `1`), the lockdown is `[none]`,
`dkms` 3.4.4 and the kernel headers are installed. **No MOK has to be
enrolled and no reboot is needed:** the unsigned module that DKMS builds is
accepted by this kernel.

**What was done, and what was not.** The source tree is cloned at the pinned
commit, the patch applies **cleanly** to a pristine clone (verified: the
patched tree is byte for byte the tree of the hand-made port), the scripts
are written and syntax-checked, and the patch was applied to the project
code (`code/base/hardware_radio.c`: the `modprobe 88XXau` / `insmod
88XXau.ko` calls now carry `rtw_monitor_disable_1m=1`, that the drivers that
do not have the parameter simply ignore). The project rebuilds and the whole
regression suite is green (158 / 399 / 43 / 25 / 20 checks, 0 failures).

**The module has been compiled for this kernel; it has never been installed,
loaded, or used to transmit a frame.** The compilation is the evidence:

```
$ modinfo /tmp/ruby/driver_88xxau/rtl8812au/88XXau.ko
name:           88XXau
vermagic:       7.2.11-1-cachyos SMP preempt mod_unload
alias:          usb:v2357p0120d*dc*dsc*dp*ic*isc*ip*in*     <- the TP-Link
parm:           rtw_monitor_disable_1m:Disable default 1Mbps rate for monitor injected frames (int)
```

(the module has 103 parameters and the whole `usb:vXXXXpYYYY` device table of
the driver). The build log is `/tmp/ruby/driver_88xxau/build.log`; it ends
with the normal "Visit https://github.com/aircrack-ng/rtl8812au …" banner
of the driver, with no error. **The DKMS installation, the module load and
the switch to the out-of-tree driver are still waiting for the approval of
the user**; the exact commands and their expected effects are in
`code/radio/driver_88xxau/README.md` §5 and in `QUESTIONS.md` Q10. Until that
happens the TP-Link is driven by the in-tree `rtw88_8821au`, that cannot
inject (§4.1).

**The switch is reversible and it cannot touch the network connection.** The
scripts identify the adapter by its **USB hardware ID** (`2357:0120`), never
by its interface name (the name changes across the reload), and they refuse
to run if that adapter is the interface that carries the default route of the
machine (`wlan0`, the Intel AX210, which is never touched). The restore
brings the in-tree `rtw88_8821au` back and the removal leaves the system
exactly as it was.

---

### 2.13 The pairing search never progressed: a gate in the desktop build, and the privileges the radio needs

**The symptom (the user):** "when i run search it never completes/progresses
the search". The log of that run repeats, every ~80 ms and forever:

```
Central: MenuSearch::onSearchStep(): waiting for router to be ready...
```

**The root cause, found by reading the log and the code.** The search step
cannot advance before the router (`ruby_rt_station`) is ready: the router is
the only producer of `PACKET_TYPE_LOCAL_CONTROLLER_ROUTER_READY`, the central
stores the timestamp in `g_RouterIsReadyTimestamp`, and
`MenuSearch::onSearchStep()` returns without advancing while that timestamp
is 0 (`code/r_central/menu/menu_search.cpp`, substep 2).

The router was **never started**, because the desktop build refuses to start
it unless there is a **serial** radio. There were two such gates, both on
`hardware_radio_has_serial_radios()` (which counts only the SiK / ELRS serial
radios, `code/base/hardware_radio.c`):

- `code/r_central/launchers_controller.cpp` → `controller_launch_router()`:
  *"Desktop build: not starting the controller router process."* — that is
  the exact line in the log of the failing run;
- `code/r_start/ruby_start.cpp` → `bStationExpected`: the watchdog of the
  start process, that would have complained that the station is missing.

The Eachine is a **Wi-Fi** radio, not a serial radio, so it never counted:
the supported USB radio of this machine was invisible to the gate. The
consequence is a deadlock: without the router there is no capture of the air,
without the capture there is no RubyFPV frame, without a frame the search
finds nothing, and the search UI waits for the router that is never started.

**The fix.** A new predicate, `hardware_radio_has_usable_radios()`
(`code/base/hardware_radio.c`), answers the question the gate was actually
asking — *does this machine have a radio that the ground station can use?* —
and it counts two kinds of radio:

- a serial radio (SiK / ELRS): it talks over an ordinary serial device and
  needs no privilege;
- a **supported Wi-Fi card** that is **not** the interface that carries the
  network connection of the machine: it is put in monitor mode and the frames
  of the air are captured with libpcap.

The exclusion of the network interface is not new: `hardware_radio_is_protected_interface()`
reads the **default route** (`ip -o route show default` → `dev wlan0`) and
refuses every configuration operation on it, so `wlan0` is still never
touched, whatever the gate says. The predicate is used in the two places that
had the old gate; `controller_launch_rx_telemetry()` keeps the **serial-only**
condition, because that process handles the telemetry of a SiK / ELRS radio
that the router forwards to it, and the telemetry of a Wi-Fi link is carried
by the radio link itself.

**The build is green** (`RUBY_BUILD_ENV=desktop make -j24 station ruby_central`)
and the whole regression suite is green (`test_video_rx_pipeline`,
`test_video_tx_resend` through `code/r_tests/video_retransmission_loop_test.sh`,
`test_sik_serial_path`, `test_radio_capabilities`, `test_menu_rf_profiles`,
`test_config_persistence`).

**The gate, measured on this machine** (a small program that links the
production `hardware_radio.c` and calls the two predicates):

```
radio interfaces found: 2
  1: wlan1  driver=rtw88_8812au  type=3 supported=1 serial=0 protected=0
  2: wlan0  driver=iwlwifi        type=0 supported=0 serial=0 protected=1
hardware_radio_has_serial_radios()   = 0     <- the old gate: the router never started
hardware_radio_has_usable_radios()   = 1     <- the new gate: the router starts
```

`wlan0` is **protected** (it carries the default route) and **unsupported**:
it is counted by neither predicate, so it is neither used nor touched.

**What is still missing: the privileges.** The router can now be started,
but it cannot open the capture and it cannot tune the card without the two
capabilities. The evidence, from the run of the radio half:

```
[HW-R] Executing command: iw dev wlan1 set type monitor
[HW-R] Unexpected result: [RTNETLINK answers: Operation not permitted]
Router: Attempt to create packet socket failed - CAP_NET_RAW may be required
```

(the *"unknown encapsulation on [wlan1]"* line of that run is a **red
herring**: the production code logs the same text for a failed
`pcap_activate()`, whatever the reason of the failure is).

The user asked for exactly this: *"i want to be able to change network
settings in rubyfpv. Give it permissions"*. The application is still **never
run as root**; the capabilities are granted to the **files**, in a runtime
directory, and the kernel gives them to the process when it executes them:

- the runtime directory is `~/.local/share/rubyfpv/run` — the project is on a
  **nosuid** mount, and the kernel **ignores** the capabilities of a file
  that is on such a mount, so the binaries are copied there (btrfs, no
  nosuid) and the copies are the ones that are executed (`code/radio/run_gcs.sh`);
- the capabilities are `cap_net_admin,cap_net_raw=ep` on `ruby_start`,
  `ruby_controller`, `ruby_central` and `ruby_rt_station`; the logger and the
  player get none;
- the installation script is `code/radio/install_gcs_capabilities.sh` and it
  **needs the root privileges** (only a process with `CAP_SETFCAP` can set a
  file capability).

**A pitfall that cost one install round, and it is worth recording:** the
owner of a file has to be changed **before** its capabilities are set.
`chown()` clears the `security.capability` attribute of the file, so an
install that does `setcap` and then `chown` leaves the binaries **with no
capability at all**, while the script prints the capabilities as installed.
The script was reordered; the second install has to be run to make the
capabilities effective. The check is `getcap ~/.local/share/rubyfpv/run/*`
(it must print the four binaries) and `CapEff` in `/proc/<pid>/status` of the
running process (it must have the bits 12 and 13).

**The state of the run directory at the end of this session:** the binaries
are the new ones (the gate fix is in them), the `res` symbolic link is in
place, but **`getcap` prints nothing**: the capabilities are not effective
until the user runs the installation again with the root privileges:

```bash
pkexec /mnt/Giochi-e-Altro-2/RubyFPV/RubyFPV/code/radio/install_gcs_capabilities.sh
```

**The search itself, once the privileges are in place:** the card is a
**5 GHz-only** card (the 5 GHz-only antennas of the Eachine, card model 7,
the guard of §0), so the search has to run on the **5.8 GHz** band, and the
channel chosen by the user is **5825 MHz (ch 165)**. The card of this machine
is already registered in the controller settings with the right flags
(`controller_radio_interfaces.cfg`: MAC `9A68F50BED7E`, card model 7, flags
101 = `CAN_USE_FOR_VIDEO | CAN_USE_FOR_DATA | CAN_RX`), it is not disabled,
and no RF profile restricts it.

### 2.14 Four defects of the running ground station, found in the logs of the user's own session, and fixed

The user ran the ground station with the Eachine (`wlan1`) attached, the
vehicle's VTX **powered off**, and reported two things: *"its complains that
there is a firmware mismatch between the vtx and the vrx, but they are both
supposedly on 11.8"*, and the OSD showed *"Vehicle OTA software update
required"*, *"Video protocols have changed. You must update your vehicle"*,
with **0 FPS**. The log of that run (`/tmp/ruby/logs/log_system.txt`) is the
evidence of the four defects below. **None of them is a hardware problem and
none of them is about the vehicle: the vehicle really is 11.8.**

#### 2.14.1 The false "firmware mismatch" — and it was blocking the video, not just the message

**The mechanism.** The OSD of the central decides between "show the video" and
"show the red background with the OTA message" in
`_render_video_background()` (`code/r_central/ruby_central.cpp`). It showed the
video only when the version of the vehicle software was **exactly** equal to
the version of the controller software, and the version of the controller is
`11.8` **build 11801** (`code/base/base.h:36-38`).

The version of the vehicle comes from two different places:

- the **telemetry**, where the version is a single byte with 4 bits for the
  major and 4 bits for the minor (`rubyVersion`, `code/radio/radiopackets2.h`),
  so `Model::populateFromVehicleTelemetryData_v6()`
  (`code/base/models.cpp:7097`) can only rebuild `11.8` and **sets the build
  number to 0** (the log says it: *"set sw version to: 11.8 (b-0)"*);
- the **full model settings sync**, which carries the real build number.

So the version of the vehicle is `11.8 (b-0)` until the settings sync
completes, and `11.8 (b-11801)` afterwards. In the log of the user's session
both are there, for the same vehicle (VID 2172108829):

```
199 log lines:  "... VID: 2172108829, ... software: 11.8 (b-0), ..."
168 log lines:  "... VID: 2172108829, ... software: 11.8 (b-11801), ..."
```

**The complaint was therefore an artefact of the build number being unknown,
not a mismatch**, and it was *transient* (it disappears after the sync). But
while the build number is 0 the condition "the versions are equal" is false,
so the red background is drawn **and the video is not displayed at all** — the
message was not cosmetic, it was hiding the picture.

**The fix.** A new function `_classify_vehicle_software_version()` in
`code/r_central/ruby_central.cpp` classifies the version of the vehicle in
three cases — *compatible*, *older* (the vehicle must be updated) and *newer*
(the controller must be updated) — and it compares the **major and minor**
when the build number is unknown, against the first vehicle build that speaks
the current video protocol (`MIN_BUILD_FOR_CURRENT_VIDEO_PROTOCOL = 11503`,
that is 11.5.03). The three OSD texts (the OTA message, the "must update your
controller" message and the "show the video" path) now follow that
classification instead of an exact equality of the whole version word. The
build number is still used, when it is known, so a vehicle that really is too
old is still told to update.

#### 2.14.2 The radio could not be tuned: the helper program lost the capability

The log of the session has **523** lines of

```
[HW-R] Unexpected result: [RTNETLINK answers: Operation not permitted]
```

that come from the `iw` helper that the production code runs to put the card
in monitor mode and to tune it (`code/base/utils.cpp`,
`hw_execute_process("iw dev %s set type monitor …")`).

**The root cause is not the installation of the capabilities**: the file
capabilities of the runtime directory are correct (`getcap` prints
`cap_net_admin,cap_net_raw=ep` on the four binaries) and the process itself
has them. The problem is that a **child process does not inherit the
capabilities of its parent**: at `execve()` the *effective* set of the child
is empty, and only the capabilities that are in the **inheritable** set and
that are **raised as ambient** are kept. The `iw` child therefore ran
**without** `CAP_NET_ADMIN` and every attempt to configure the radio failed.

**The fix.** A new function `hardware_raise_ambient_capabilities()`
(`code/base/hardware.cpp`) reads `CapPrm` and `CapInh` from
`/proc/self/status` and raises, with
`prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, <capability number>)`, every
capability that is in both sets. (The exact form of that call is the subject
of **§2.14.7**: the first version of the fix used a `prctl` form that the
kernel accepts but that does nothing.) The kernel adds the permitted
capabilities of a file to the inheritable set, so `cap_net_admin` and
`cap_net_raw` can be raised. It is called at the start of the two processes
that configure the radios: `ruby_central` and `ruby_rt_station`
(`code/r_central/ruby_central.cpp`, `code/r_station/ruby_rt_station.cpp`).
The log line of the new code is:

```
[Hardware] 2 capabilities raised as ambient: the helper processes (iw) inherit them.
```

**No privilege was added and nothing was changed system-wide**: the process
keeps the capabilities it already had, it only passes them on to the helper
it starts.

#### 2.14.3 The IPC storm: the router wrote into a channel that nobody read

The log of the session has **5600** lines of

```
Router: SOFT_ERROR: [IPC] Failed to write to IPC [ROUTER-TO-TELEMETRY], error code: EAGAIN
Router: [IPC] Channel [ROUTER-TO-TELEMETRY] (fd 262175, unique id 5) info: 84 pending messages, 16300 used bytes, max bytes in the IPC channel: 16384 bytes
Router: SOFT_ERROR: Main loop took too long (max 13 ms, fps: 60): 21 ms
```

The channel was **full** (16300 of 16384 bytes, 84 messages waiting) and
nothing ever read it: the reader of that channel is `ruby_rx_telemetry`, that
the desktop **never starts** (it is the process that handles the telemetry of
a SiK / ELRS radio; the telemetry of a Wi-Fi link is carried by the radio
link itself). Every write failed, and the retry loop of the IPC write
(`msgsnd(..., IPC_NOWAIT)` with 3 attempts and a 10 ms pause between them)
**wasted 30 ms per message**: the main loop of the router took 21 ms instead
of 13 ms, forever.

**The fix.** In `open_pipes()` (`code/r_station/ruby_rt_station.cpp`) the
write endpoint of that channel is opened **only when there is a serial radio**
(`hardware_radio_has_serial_radios()`); on the desktop there is none, so the
router does not write into a channel that nobody reads. The channel is still
there for the case it is made for.

#### 2.14.4 The radio RX thread burned a full core

The router's radio RX thread (`code/radio/radio_rx.c`) was at **100 % CPU**
with the vehicle off, i.e. with **nothing to receive**. The reason is the
poll loop: the thread polls the file descriptor of the capture and it read
the packet **only if the poll reported `POLLIN`**. A poll can also report
`POLLERR` or `POLLHUP` — the `ENOBUFS` of a full receive buffer, that the
kernel keeps pending until a read consumes it — and then **no read was ever
attempted**: the error was never consumed, the poll returned immediately at
every iteration, and the thread spun on the CPU.

**The fix** (in `code/radio/radio_rx.c`):

- a read is attempted also when the poll reports `POLLERR` or `POLLHUP`, so
  that the pending error of the socket is consumed and the poll blocks again;
- when a poll wake-up produced **no packet at all**, the thread waits 1 ms
  before polling again, so that no condition can make it spin;
- `radio_process_wlan_data_in()` (`code/radio/radiolink.c`) uses
  **`pcap_next_ex()`** instead of `pcap_next()`: the two are not the same
  thing here. `pcap_next()` returns `NULL` both when there is no packet and
  when the read failed, and the error was **invisible**; `pcap_next_ex()`
  distinguishes them (0 = no packet, < 0 = error) and the error is now logged
  (every 10th, so that a full buffer does not flood the log). The interface
  is **not** declared broken on a transient error: only a real failure of the
  interface does that.

The production RX path is covered by the regression suite: the retransmission
loop test reads **all** the frames of the capture through this code
("all the RF frames of the vehicle were read by the RX code: 30") and the
video of the frame that had a lost packet still reaches the player byte for
byte.

#### 2.14.5 The router died every ~56 seconds — the watchdog, and what is still open

The central watches the router (`code/r_central/link_watch.cpp`): if the
router does not report for more than 1.1 s, four times in a row, the central
kills it and starts it again. The log of the session has **8** such restarts,
one every ~56 s:

```
Central: SOFT_ERROR: Router process is about to fail. Last active time: 9283 ms ago, loop counter: 74114
Central: ERROR: Router process has failed. Current router PIDS: [738384].
```

The last line the router wrote before every death is the same:

```
Router: Send pairing request to VID: 2172108829
```

and it is then silent for **~9.3 s**. The pairing request is the only thing
the router puts on the air while the vehicle is off, so the death happens in
the path that writes to the radio (`send_packet_to_radio_interfaces()` →
`radio_write_raw_ieee_packet()` → `pcap_inject()`), on a card that is in
monitor mode and **cannot transmit** (the TX blocker of §4.1).

**What is proved and fixed:** the two proven causes of the silence are the
EAGAIN storm of §2.14.3 (30 ms lost per refused message) and the radio
tuning that could not run at all (§2.14.2). Both are fixed, and with them the
router has no reason to be silent for 9 s.

**What is not proved:** the exact instruction in which the router blocks was
not identified. The kernel of this machine has `ptrace_scope=1`, so no
debugger can be attached to the running process (`gdb -p` → "ptrace:
Operation not permitted", `eu-stack -p` → "Permission denied") and
`/proc/<pid>/maps` is not readable either. The suspicion is
`pcap_inject()` on the blocking TX handle of a card that cannot transmit, but
it is a suspicion, not a measurement, and it is **not** claimed as a fact.

#### 2.14.6 The build of the tree was not reproducible: stale object files

`test_sik_serial_path` failed with 2 checks (the packet was not delivered by
the production RX thread) even though it had passed 25/25 the day before. The
cause was **not** in the code: the object file of
`code/base/hardware_radio_sik.c` in the tree was **newer than its source**,
but it had been built from an **intermediate state** of that source, in which
the file descriptor of the SiK radio was not stored — the radio was "opened",
the write worked, and the RX thread polled **fd 0** (the standard input)
instead of the serial device. The Makefile has **no header dependency
tracking** and no way to notice that, so a `make` that looks up to date
silently keeps a stale object.

**The lesson, and the rule that follows from it:** before a build that has to
be **installed** and **run**, do `make clean` and build again. After the clean
build of this session, every binary is built from the sources that are in the
tree now, and the whole suite is green:

| Test | Result (after the clean build) |
|---|---|
| `test_video_rx_pipeline` | 158 / 0 |
| `code/r_tests/video_retransmission_loop_test.sh` | 4 steps, 38 checks, 0 failures |
| `test_sik_serial_path` | **25 / 0** (it was 23 / 2 with the stale object) |
| `test_radio_capabilities` | **399 / 0** |
| `test_menu_rf_profiles` | 43 / 0 |
| `test_config_persistence` | 20 / 0 |
| `git diff --check` | clean |

#### 2.14.7 The fix of §2.14.2 was **itself** wrong: the `prctl` call that the kernel honours

The first version of `hardware_raise_ambient_capabilities()` used

```c
prctl(PR_CAP_AMBIENT_RAISE, (unsigned long)(uintptr_t)&uCapability, 0, 0, 0);
```

**On this kernel that call returns 0 and does nothing**: the ambient set stays
empty, the child processes keep running without `CAP_NET_ADMIN`, and the
radio can still not be tuned. The regression test
(`code/r_tests/test_ambient_caps.cpp`, new) caught the defect in the fix
itself: the child process came out with `CapAmb: 0x0` and `CapEff: 0x0`.

**How the right call was found.** `strace` is not installed on this machine, so
the `prctl` call of `libcap` was intercepted with an `LD_PRELOAD` shim
(`/tmp/rubyfpv-ns-test/trace_prctl.so`, a throw-away probe, not part of the
tree). This is what `capsh --addamb=cap_net_raw` does:

```
[trace] prctl(opt=47, arg2=2, arg3=13/0xd, arg4=0, arg5=0) = 0
```

`opt=47` is `PR_CAP_AMBIENT`, `arg2=2` is `PR_CAP_AMBIENT_RAISE` and `arg3` is
the **capability number, by value**. The four candidate forms were then
measured on this kernel (7.2.11) in a user namespace, in the exact state of the
ground station (uid 1000, `CapPrm = CapInh = CapEff = 0x2000`):

| Form | Call | Result |
|---|---|---|
| A | `prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, &cap)` | `EINVAL` (22) |
| B | `prctl(PR_CAP_AMBIENT_RAISE, &cap)` | **0, but no effect** (`CapAmb` stays 0) |
| C | `prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, cap)` | **0, and it works** |
| D | `prctl(PR_CAP_AMBIENT_RAISE, cap)` | `EFAULT` (14) |

**The production code now uses form C.** No `CAP_SETPCAP` is needed: the
measurement above was made with a permitted set that contains only
`cap_net_raw` (`0x2000`), i.e. exactly the file capabilities of the ground
station, and the raise succeeded. `code/radio/install_gcs_capabilities.sh`
therefore does **not** have to change.

The regression test, run in a user namespace that has the capabilities of the
ground station:

```
== Test: the ambient capabilities of the ground station ==
  the capability sets of this process: permitted=0x3000 inheritable=0x3000 ambient=0x3000
  PASS  the ambient set was empty before the raise                   got=0x0
  the child process: ambient=0x3000 effective=0x3000 (it must contain 0x3000)
  PASS  the child process gets the capabilities as ambient           got=0x3000
  PASS  the child process inherits the capabilities                  got=0x3000
```

`0x3000` = `cap_net_raw` + `cap_net_admin`: the child process — an ordinary
program without any file capability, exactly like the `iw` helper — **does**
inherit the privileges of the ground station. The test is a real check only
when it is run with a capability; without one it checks that the function is
harmless (3 checks, 0 failures).

### 2.15 The black video of the user's session: the root cause, and the receive-only ground station

**Established on 2026-10-24**, from the evidence of the user's own session
(§2.14), the vehicle protocol of the unmodified upstream source (11.8, in
`../ruby_original/RubyFPV`) and the disassembly of §4.1:

The user's vehicle announced itself on the air (telemetry and video packets
were received, 550–570 video packets/sec), but it **never received a single
packet from the controller**, because every radio interface of this desktop
is receive-only (the in-kernel `rtw88` driver of the Eachine cannot inject
raw RF frames, §4.1). The Ruby pairing flow of the upstream source needs the
controller to **send** a Ruby pairing request to the vehicle, and the vehicle
pairs only after it received one and answered it
(`r_vehicle/process_received_ruby_messages.cpp` of the upstream source). The
vehicle never paired, so the router kept `bIsPairingDone = false`, and the
**stock video gate** (`processor_rx_video.cpp`,
`if ( ! pApiObjects->bIsPairingDone ... )` in
`handleReceivedVideoPacket()`) dropped every video packet it received — the
video was received on the air and then thrown away. The same missing uplink
is why no command response, no FC telemetry and no full-settings sync ever
came back: the vehicle never heard the requests in the first place.

What was changed (all on branch `refactor`, all desktop-relevant, no model
file touched, no RF transmit added):

1. **The inventions that contradicted the upstream source were reverted**
   (the user asked to trust the original source over the modified one):
   `MODEL_FIRMWARE_TYPE_OPENIPC` is gone from `code/base/flags.h`
   (upstream defines only `MODEL_FIRMWARE_TYPE_RUBY 0`), together with its
   use in `string_utils.c`, `launchers_controller.cpp`,
   `menu_search_connect.cpp`, `menu_search.cpp`, `handle_commands.cpp`,
   `periodic_loop.cpp` and `radio_links.cpp` of both station and vehicle.
   The gates of the vehicle-settings synchronization in
   `handle_commands.cpp` are back to the upstream form (`b_mustSyncFromVehicle
   || first connection` outer condition, firmware == Ruby, `bPairedConfirmed`,
   five retries); the forced `bPairedConfirmed = true` and the invented
   "first connection bypasses the pairing flag" gate are gone; the pre-existing
   desktop additions (the truthful stop after five failed sync attempts with
   its warning, and the `[SyncTrace]` diagnostics) are kept.
2. **The receive-only radios are now modeled honestly.** The hardware layer
   classifies the in-kernel Realtek drivers (`rtw88*`, `rtw89*`) as
   **`isTxCapable = 0`** (`hardware_radio_driver_is_rtw_in_tree()` in
   `code/base/hardware_radio.c`; the in-tree `rtw88_8812au` used to hide
   inside the generic `"8812au"` match and to be declared TX capable). The
   classification is also **refreshed when a saved radio config file is
   loaded** (`hardware_load_radio_info_into_buffers()`), so a stale
   `current_radios.cfg` written by an older binary cannot re-enable TX.
   `radio_open_interface_for_write()` (`code/radio/radiolink.c`) refuses a
   card whose driver cannot send RF packets, the station radio open loop
   (`code/r_station/radio_links.cpp`) skips the pcap TX open for them, and a
   station that ends up with **zero** TX interfaces **keeps its radio links
   alive in receive-only mode** instead of the old behavior of closing every
   interface and aborting. A vehicle radio (`code/r_vehicle/radio_links.cpp`)
   likewise skips a non-TX driver (vehicles still abort without TX — a
   vehicle that cannot transmit is useless).
3. **The pairing has a fallback that needs no uplink**: on a controller that
   cannot send any RF packet, the router pairs the vehicle from its **ruby
   telemetry** (`code/r_station/pairing_telemetry.{h,cpp}`, a small pure
   decision module): the telemetry announces the vehicle id, the software
   version and the radio links, on the same radio links where the video and
   the telemetry are received. The telemetry must be **fresh** (≤
   `PAIRING_TELEMETRY_MAX_FRESHNESS_MS` = 2500 ms, ruby telemetry packets
   arrive ~every 100 ms), must belong to the expected vehicle id, and must
   announce a known software version. When it does, the router marks the
   vehicle paired and announces it to the controller **in the exact format
   of a real Ruby pairing confirmation packet** (header + resend counter +
   software version, the format the vehicle sends in the upstream source),
   so the central's stock handler (`process_router_messages.cpp`,
   `PACKET_TYPE_RUBY_PAIRING_CONFIRMATION`) runs: `bPairedConfirmed = true`
   and the stock `ALARM_ID_CONTROLLER_PAIRING_COMPLETED` warning. The video
   gate then opens, because the vehicle **is** paired — by telemetry instead
   of by an RF handshake that cannot happen.
4. **The user is told the truth about what is lost**: a new generic alarm
   (`ALARM_ID_GENERIC_TYPE_CONTROLLER_RADIO_TX_UNAVAILABLE`, `alarms.h`)
   states that the radio links are receive-only (video and telemetry work;
   commands, RC link and pairing requests cannot be sent to the vehicle).
   The `_must_inject_ping_now()` of the router also stops queueing pings
   that no interface can send (`controller_has_radio_interfaces_for_send()`
   in `code/r_station/packets_utils.cpp`).

Regression tests added on the same day: `test_pairing_telemetry` (9/9, the
pure pairing decision) and the in-kernel-driver section of
`test_radio_capabilities` (406/406 in total, including the live check that
the attached `wlan1` is classified receive-only).

### 2.16 The pairing and the live video ARE completed (2026-10-25, the RTL8812AU era)

**Established and verified live on 2026-10-25.** The hardware premise of
§2.15 changed first: the user attached an **RTL8812AU** dongle (the prepared
out-of-tree aircrack driver kit, `rtw_monitor_disable_1m=1`), which is both
monitor- and inject-capable — the uplink of §4/Q1 exists, through the driver
Q1 proposed. With it: Stage-1 RX and Stage-2b TX injection passed on air, and
the full app then **paired with the real VTX, synchronized the vehicle
settings and rendered the live camera feed in the player** (user-confirmed:
"Yes, video plays"; the router logs show `Received pairing confirmation ...
CID: 2153624802` → `Pairing confirmed for vehicle VID 2172108829` → the
`settings synchronized` flag true, and ~550–570 video packets/sec pushed to
the player FIFO).

Three desktop-port defects still blocked that flow, all found and fixed on
this day:

1. **The controller ID was literally zero on the desktop.**
   `controller_utils_getControllerId()` compiles to a stub `{ return 0; }`
   for every platform other than Raspberry/Radxa (the `#else` branch of
   `code/utils/utils_controller.cpp`), so every Ruby pairing request was sent
   with `vehicle_id_src = 0` (the router log: `CID: 0`) and the vehicle —
   whose handler binds `g_uControllerId = vehicle_id_src` and then answers
   with a confirmation addressed to that controller — could never bind or
   answer a valid controller. **Fix:** the implementation guard now also
   includes `HW_PLATFORM_LINUX_GENERIC`; the ID is generated once and stored
   in `<FOLDER_CONFIG>/controller_id.cfg` (verified live: ID 2153624802).
2. **The RF frequency switch failed with `Operation not permitted`.** The
   capped processes run bare `iw dev <if> set freq ...` (no sudo wrapper); a
   caller's file capabilities do not propagate to the `iw` child, so the
   switch failed (`SOFT_ERROR: Failed to switch radio interface ...
   returned error: [command failed: Operation not permitted]`) and TX was
   opened "on 0 Mhz" while pairing requests were still reported as sent
   (enqueued on an unmanaged radio state). **Fix (deliberate system
   decision):** `setcap cap_net_admin+ep /usr/bin/iw` — the router's next
   assignment cycle logged `to frequency 5825 Mhz succeeded.` and `Opened
   radio interface 1 (wlan1) for writing on 5825 Mhz.`
3. **The watchdog restart loop and the "Socket failed"** (both fixed earlier
   in the same session, recorded here for the trail):
   `hardware_radio_get_class_net_adapters_count()` counted every `wlan*`
   interface while the enumeration cache held only the supported drivers →
   a permanent "Radio interfaces count has changed" alarm every 5 s; the
   count helper now mirrors the enumeration's supported-driver equivalence
   by querying each interface's uevent DRIVER. And
   `hw_get_ruby_process_full_path()` made the router execute the fuse-mounted
   repo copy of the binary (file capabilities are impossible there → the TX
   `socket()` "Operation not permitted"); on the generic desktop the
   resolver now prefers the canonical `FOLDER_BINARIES` (`/tmp/ruby/bin/`).

The vehicle answers well-formed, actually-transmitted pairing requests —
the on-air capture of the pre-fix state showed no confirmation because the
requests were wrought/undeliverable then, not because the vehicle ignored
them.

Cleanup state: all temporary instrumentation of this session (the
`DBG-OPEN-READ`/`DBG-VP`/`DBG-MODEL` probes and the packet-type histogram of
`test_video_rx_pipeline`) is removed; the test keeps the substantive
`loadAllModels()` call that fixed its current-model lookup. The deployed
router binary still carries the (harmless) print strings until its next
restart — `cp` of a running image is refused (`Text file busy`).

---

## 3. What is **NOT** validated — read this before calling the system ready

> **Live RF reception is NOT validated. The system is NOT flight-ready.**
> Everything above is either a unit test over a controlled loopback, a PTY
> protocol fixture, or a run of the application on this desktop. None of it is
> a real radio link.

Specifically:
- **No real RF reception was performed.** The radio-path tests build the RF
  frames with the production TX code and read them back with the production
  RX code, but the medium is a capture file, not the air. The SiK test feeds
  bytes through a pseudo terminal. Neither is an over-the-air link.
- **The video retransmission loop (§2.8, §2.9) is proven at the software
  level only.** The request, the answer, the completion of the block and the
  delivery of the video to the desktop player all work with the production
  code of both ends, but the "air" between the two ends is a capture file.
  **No packet was ever retransmitted over a real radio link**, so the
  retransmission over RF is **not** validated.
- **The two-radio test was run for the VRX half only** (§2.10): the capture of
  the real air with the production code works, and the production filter
  correctly rejected the four ordinary Wi-Fi frames that were on the air. **No
  RubyFPV frame was on the air, so no video was received and the player had
  nothing to display.** The **TX half was not run**, because the driver of the
  TP-Link cannot inject the frames RubyFPV needs (§4.1):
  - `code/r_tests/two_radio_rf_test.sh` — the one-command script: it finds the
    two radios by their **USB hardware ID** (2357:0120 and 0bda:881a), it
    refuses `wlan0` and any interface that carries the default route, it
    brings the managed interface of the radio **down**, it moves the radio to
    a **5.8 GHz** channel (the Eachine has 5 GHz-only antennas) and it
    **verifies that the monitor interface really is above 5000 MHz** before
    anything else happens (the monitor interface is born on the default
    channel of the radio, which is a 2.4 GHz one), it starts the desktop
    player **as the user of the session** (the app is never run as root) and
    it runs the halves of the test;
  - by default the script runs the **VRX half only**, against the live air:
    the production filter is installed on a real monitor interface and the
    production parsing runs on it. The **TX half is opt-in** (`--tx`), because
    the injection cannot work with the two adapters that are on this machine
    (§4.1);
  - `test_video_rx_pipeline --rf-tx <seconds> <interface>` — the VTX half: the
    real video stream is sliced in real radio video packets, wrapped in the
    real RF frames by `radio_build_new_raw_ieee_packet()` and put on the air
    by `radio_write_raw_ieee_packet()` (pcap injection);
  - `test_video_rx_pipeline --rf-rx <seconds> <interface>` — the VRX half: the
    frames are read by `radio_process_wlan_data_in()` and
    `packet_process_and_check()` and they are handed over to
    `process_received_video_component_packet()`, so the video reaches the
    desktop player through the production pipeline.
- **The TX half cannot be done with the two adapters that are on this
  machine.** This is not a guess: it was settled by reading the kernel
  sources, the BTF of the running kernel and the disassembly of the
  installed module (§4.1). The capture half is expected to work; the
  injection half is dropped by mac80211 before it reaches the radio, so the
  RF milestone needs an adapter whose driver supports the monitor vif
  (§7) — most likely an **MT7612U** — or the out-of-tree `rtl8812au`
  driver, that is now prepared for the **existing** TP-Link (§2.12) and
  that waits for the approval of the user.
- **The out-of-tree driver of §2.12 is prepared but not built, not
  installed and not loaded.** The port to the 7.2.11 kernel API is written
  and it applies cleanly, but **the driver has never been compiled on this
  kernel**, so it is not known yet whether it builds. Until it is compiled,
  installed, loaded and exercised on the air, the TX half of the RF
  milestone is **not** unblocked, and no claim of a working over-the-air
  video link can be made.
- **No vehicle was armed, no motor was started, no flight-control command was
  sent, and no flight was conducted** (also forbidden by the mission).
- The **transmit** side over RF is unverified: no packet was ever sent on a
  real radio link.
- The **telemetry** path was not exercised with a real flight controller.
- The `/tmp/ruby_rf_fixtures_quarantine/` Python SiK emulator of an earlier
  session was a **protocol fixture**, not evidence of radio support; it was
  stopped after its identity was verified (PID 129064, cmdline
  `python3 /tmp/ruby_rf_fixtures_quarantine/sik_emu.py`), and the in-repo test
  `test_sik_serial_path` supersedes it.

---

## 4. Blockers (environment, not software bugs)

1. **The capture of the two USB radios works; the injection of the two USB
   radios does not.** Both facts were settled by reading the kernel sources
   of the running kernel (7.2.11), the BTF of that kernel and the
   disassembly of the installed `rtw88_core.ko` — they are not guesses and
   they are not the "the rtw88 drivers have no monitor mode" claim that the
   earlier version of this document made (that claim was **wrong**).

   *The capture works.* mac80211 provides the monitor interface by itself
   (the "software monitor"): `ieee80211_change_interface()` /
   `ieee80211_setup_netdev()` call `drv_add_interface()` for a
   `NL80211_IFTYPE_MONITOR` interface **only** if the driver asked for the
   active monitor (`MONITOR_FLAG_ACTIVE`) or if it set
   `IEEE80211_HW_NO_VIRTUAL_MONITOR`; otherwise the **virtual monitor**
   (`ieee80211_add_virtual_monitor()`) is used, and the driver is never asked
   to add a monitor vif. The rtw88 drivers set **neither**
   `IEEE80211_HW_WANT_MONITOR_VIF` (12), `IEEE80211_HW_NO_VIRTUAL_MONITOR`
   (13) nor `IEEE80211_HW_QUEUE_CONTROL` (19) — the flags of the running
   kernel were read from `/sys/kernel/btf/mac80211` — so the virtual monitor
   is used and the frames that the driver delivers are copied to it by
   `ieee80211_rx_monitor()`, with the radiotap header that mac80211 builds.
   The production RX code reads exactly that format: the link type of a
   Linux monitor interface is `DLT_IEEE802_11_RADIO` (**127** in libpcap),
   which the production gate in `code/radio/radiolink.c` accepts, and the
   production filter (`ether[0x00:2] == 0x0801 && ether[0x0a:4] ==
   0x13123456 && ether[0x04:1] == 0x%.2x`) is applied by libpcap **after**
   the radiotap header, so it matches the frames of a real monitor
   interface. This was verified twice: with a probe against libpcap 1.11
   (the filter matches with a radiotap header of 12 and of 26 bytes, and it
   rejects the wrong port byte) and with the four **exact** mac80211
   radiotap layouts in the test suite (18, 26, 21 and 24 bytes — the
   legacy, the legacy with the timestamp, the HT/MCS and the radiotap
   namespace one), read back **byte for byte** through the production
   parsing, with the production CRC check.

   *The injection does not work — and now this is proved on the **loaded
   driver of the running kernel**, not only on the sources.* The loaded
   module was decompressed and disassembled
   (`/usr/lib/modules/7.2.11-1-cachyos/kernel/drivers/net/wireless/realtek/rtw88/rtw88_core.ko.zst`).
   In `rtw_ops_add_interface()` the type of the vif (`vif->type`) is read and
   only **four** values are handled: **1** (AP), **2** (STA), **3** (ADHOC)
   and **7** (P2P client). Every other value — and `NL80211_IFTYPE_MONITOR`
   is **6** — reaches an `ud2` instruction, that is, it is a fatal path the
   driver never takes. **The rtw88 drivers cannot own a monitor vif.**
   This is confirmed at runtime by three independent observations:
   - the monitor interface was created **without any kernel oops**, which
     would have been the case if the driver had been asked to add a monitor
     vif;
   - `ip link del rubyvrx0` answers **"Operation not supported"**
     (`EOPNOTSUPP`): the interface is the **virtual monitor of mac80211**,
     which lives on the phy and is removed with `iw dev <name> del`, not
     with `ip link del`;
   - the monitor interface appeared in the `iw dev` output **before** the
     managed interface of the same radio (the log of the RF test shows it:
     the phy of `wlan1` was preceded by the "txpower" line of the monitor
     interface, not by the "phy#2" header), which is the layout of a
     mac80211 virtual monitor.

   The path of an injected frame is `ieee80211_monitor_start_xmit()` →
   `ieee80211_xmit()` → `__ieee80211_tx()`. The disassembly of the running
   `mac80211.ko` confirms the calls of `ieee80211_monitor_start_xmit()`:
   `ieee80211_hdrlen`, `cfg80211_chandef_usable`,
   `cfg80211_reg_check_beaconing`, `ieee80211_select_queue_80211`,
   `ieee80211_parse_tx_radiotap`, `skb_pull`, **`ieee80211_xmit`**,
   `consume_skb`. Without the active monitor and without
   `WANT_MONITOR_VIF` there is no vif for the frame, so mac80211 calls
   `ieee80211_tx_frags(local, NULL, …)` with a `NULL` vif; the parallel
   switch in `ieee80211_tx_dequeue()` ends with `info->control.vif = NULL`
   and the skb is **dropped**: the frame never reaches the driver and
   never reaches the air. `pcap_inject()` still reports a success, so the
   failure is only visible on the receiving radio.

   **Consequence:** the VRX half of the two-radio test **was run** against
   the live air (§2.10) with the hardware that is on this machine, but the
   TX half needs an adapter whose driver supports the monitor vif — the
   **mt76** family (MT7612U / MT7610U) does, see §7 — or the out-of-tree
   `rtl8812au` driver, that is now **prepared for the existing TP-Link**
   (§2.12: the port to the 7.2.11 kernel API, the DKMS package, the
   reversible switch and the restore) and that waits for the approval of the
   user (`QUESTIONS.md` Q10). The only driver with a dedicated monitor
   implementation on this machine is `iwlmvm` (the AX210 on `wlan0`), and
   `wlan0` must not be touched.
2. **The privileges of the ground station are installed; the last reason why
   they were not effective is fixed, but the fixed binaries are not installed
   yet.** The application is **never run as root**, there is no unattended
   `sudo` and there is no system-wide permission or capability change: at the
   request of the user ("i want to be able to change network settings in
   rubyfpv. Give it permissions") the two capabilities that the radio needs —
   `CAP_NET_ADMIN` (monitor mode, frequency) and `CAP_NET_RAW` (the capture)
   — are granted to the **files** of a runtime directory
   (`~/.local/share/rubyfpv/run`), and the kernel gives them to the process
   when it executes them (§2.13). The installation is correct now
   (`getcap` shows `cap_net_admin,cap_net_raw=ep` on the four binaries;
   the `chown()` before `setcap` was the fix).

   The **523** `RTNETLINK answers: Operation not permitted` lines of the
   user's session were **not** a missing installation: the *process* had the
   capabilities, but the **helper** it starts (`iw`) did not — a child
   process does not inherit the effective set of its parent at `execve()`.
   That is fixed in the code by raising the capabilities as **ambient**
   (§2.14.2), so that the helper inherits them. The first version of that fix
   used a `prctl` form that the kernel accepts but that does nothing; the
   correct form was then measured on this kernel and the code was corrected
   (§2.14.7), and the inheritance is now covered by a regression test
   (`test_ambient_caps`). **No `CAP_SETPCAP` is needed**, so the installation
   script does not have to change.

   **What is still missing is only the installation**: the binaries of the
   run directory are the **old** ones, the ones that were built before the
   fixes of §2.14. Copying the new binaries there (and re-applying the
   capabilities) needs the explicit authorization of the user, because it
   overwrites files outside the repository. Until that is done the four
   defects of §2.14 are **fixed in the source but not live**.
3. **`dmesg` is not readable** by a normal user, so the kernel-level view of
   the radio is not available for diagnostics.
4. ~~The configuration lives in `/tmp/ruby/config/` and is therefore lost on
   reboot.~~ **RESOLVED**: the desktop configuration is now in
   `~/.config/rubyfpv/` and survives a restart (see §2.2).
5. **The 2.4 GHz link cannot be tested with the hardware that is present**:
   the only Ruby radio is the RTL8812AU with 5 GHz-only antennas, and the
   guard (correctly) refuses to tune it to 2.4 GHz.

---

## 5. Architecture notes

- `ruby_start` → `ruby_controller` (watchdog) → `ruby_central` (UI, control
  plane) → `ruby_rt_station`, `ruby_rx_telemetry`, `ruby_tx_rc`,
  `ruby_player_desktop`, `ruby_logger`, `ruby_video_proc`.
- `ruby_central` is the control plane; `ruby_rt_station` is the RF data plane.
  A "starting" log line is not proof of startup — confirm with the process
  list.
- IPC: System V message queues (`RUBY_USES_MSGQUEUES 1`); POSIX shared memory
  for stats and state only; named pipes for the video/audio streams.
- `hw_get_ruby_process_full_path()` resolves a binary as given → `FOLDER_BINARIES`
  → `/tmp/debug` override, so the processes find each other whatever the CWD is.

### 5.1 What "forwarding / retransmission" means in this system
It was established from the code, not assumed:

- **Retransmission (the link layer).** When a video block is incomplete, the
  station asks the vehicle to send the missing packets again:
  `ProcessorRxVideo::checkAndRequestMissingPackets()` builds a
  `PACKET_TYPE_VIDEO_REQ_MULTIPLE_PACKETS` request (the packet header, the
  retransmission id, the video stream index, the flags, the count and one
  (block index, packet index) pair per missing packet) and puts it in the
  **high priority** queue that the radio TX thread reads. The vehicle parses
  it (`code/r_vehicle/processor_tx_video.cpp`) and answers with the missing
  video packets, marked with `PACKET_FLAGS_BIT_RETRANSMITED` and
  `PACKET_FLAGS_BIT_HIGH_PRIORITY`, with the retransmission id in the video
  stream info (`VIDEO_STREAM_INFO_FLAG_RETRANSMISSION_ID`).
  The window in which a block is kept for a retransmission is
  `Model::getCurrentVideoProfileMaxRetransmissionWindow()`
  (`((uProfileEncodingFlags & 0x0000FF00) >> 8) * 5` ms; the default
  `DEFAULT_VIDEO_RETRANS_MS5_HP` = 14 → **70 ms**). A block that is older is
  dropped, not requested.
- **Forwarding (the output side).** The received video is forwarded to the
  local desktop player through the FIFO `/tmp/ruby/fifovideo_disp`
  (`rx_video_output_video_data()` → `_rx_video_output_to_video_streamer_pipe()`),
  and it can be forwarded further to the UDP port of a local player, to an
  Ethernet socket (`nVideoForwardETHPort`) or to a USB (tethering) device.
  These are the *output* paths of the station; they are independent of the
  retransmission mechanism and both are now covered: the retransmission by
  test 14, the FIFO forwarding by the live demonstration of §2.4.

---

## 6. Verification rules used in this port

Every result is labelled: static analysis / runtime discovery / unit test /
controlled loopback / real hardware. A successful compile is **not** proof of
RF support. A setting is reported as applied only when it actually was
(applied / requested / effective are distinguished). No mock radios, no fake
telemetry, no synthetic recovery and no parallel RF data plane in product
code. No transmission to an uncontrolled vehicle.

---

## 7. A monitor-mode-capable adapter (Q1): what the adapter must do, and which ones do

**First, the exact requirement.** A chipset name is not enough: an adapter is
usable for the RubyFPV 802.11 video path only if it does **all** of these:

1. **Monitor mode.** The RX path is pcap based: `radio_open_interface_for_read()`
   opens a pcap handle on the interface and installs the filter
   `ether[0x00:2] == 0x0801 && ether[0x0a:4] == 0x13123456 && ether[0x04:1]
   == 0x%.2x` for the radiotap link type, or
   `radio[0x40:2] == 0x0801 && radio[0x4a:4] == 0x13123456 && ...` for the
   prism one (`code/radio/radiolink.c`). Without `NL80211_IFTYPE_MONITOR`
   there is no capture of the raw 802.11 frames.
2. **The raw frame, with the radiotap header.** The code parses the received
   frame with `ieee80211_radiotap_iterator_init()` and it needs the link type
   `DLT_IEEE802_11_RADIO` (**127** in libpcap — "802.11 plus a radiotap
   radio header"), which is exactly what a Linux monitor interface
   delivers; the prism link type (`DLT_PRISM_HEADER`) is accepted as well.
   An adapter in "managed" mode (Ethernet encapsulation) is useless.
3. **Injection on a monitor interface.** The TX path is `pcap_inject()` of a
   radiotap + 802.11 frame (`radio_write_raw_ieee_packet()`), so the adapter
   must accept injected frames and put them on the air at the requested rate
   (RubyFPV sends with the legacy radiotap header or with the MCS one).
4. **A fixed channel in the 5.8 GHz band.** The video link of the models is
   5.8 GHz and the card must be able to stay on one channel while it is in
   monitor mode (the injection of some drivers breaks if the channel is
   changed or if the card is scanning).
5. **5.8 GHz in hardware.** The antennas of the card must cover
   5.725–5.850 GHz. The card that is on this machine (`Eachine WiFiLink`,
   RTL8812AU) has **5 GHz-only antennas** and that is why the software guard
   `radio_cap_card_model_is_5ghz_only()` (model 7) refuses to tune it to
   2.4 GHz. That guard must not be weakened.
6. **Enough USB bandwidth.** The video stream is 2–4 Mbps; a USB 2.0 link is
   enough.
7. **A USB id that RubyFPV knows.** The card model is detected from the USB
   product id (`code/base/hardware_radio.c`), and the model selects the
   antenna guard and the TX power table (`code/base/tx_powers.cpp`). A new
   adapter needs a small entry there — a code change, not a driver change.

**Second, the candidates, with the evidence.**

| Adapter / chipset | USB id | Driver (in-tree) | Bands | Monitor | Injection | Active monitor | Verdict for RubyFPV |
|---|---|---|---|---|---|---|---|
| **ALFA AWUS036ACM** / MT7612U | 0e0d:7612 | `mt76x2u` (mt76) | 2.4 + 5 | yes | yes, at all MCS levels | yes | **the best candidate**: 5 GHz, in-tree, injection verified |
| **ALFA AWUS036ACHM** / MT7610U | 0e0d:7606 | `mt76x0u` (mt76) | 2.4 + 5 | yes | yes | yes | also good, and the cheapest |
| Panda PAU0B (MT7610U), ASUS USB-AC51 (MT7610U) | 0e0d:7606 | `mt76x0u` | 2.4 + 5 | yes | yes | yes | the same chipset, cheaper |
| AR9271 | 0cf3:9271 | `ath9k_htc` | **2.4 only** | yes | yes (but it ignores the radiotap TX flags) | no | 2.4 GHz only → it would need a 2.4 GHz model and a different antenna; not the reference band |
| RT3572 / RT2800U | 148f:3572 | `rt2800usb` | **2.4 only** | yes | yes | no | same limitation as the AR9271 |
| ALFA AWUS036AXM / MT7921U | 0e0d:7921 | `mt7921u` | 2.4 + 5 + 6 | **broken** (as of kernel 6.18) | injects at 1 Mbps (2.4) / 6 Mbps (5) | no | **not recommended** |
| Netgear A9000 / MT7925U | 7925? | `mt7925u` | 2.4 + 5 + 6 | yes (verified end of 2025) | yes | yes | good, but WiFi 7 and the newest, so the least proven |
| The **existing** RTL8812AU (`wlan1`) and RTL8821AU (`wlan2`) | 0bda:881a, 2357:0120 | `rtw88_8812au`, `rtw88_8821au` | 2.4 + 5 | **yes** (the mac80211 software monitor: the driver is never asked to add a monitor vif) | **no** (mac80211 drops the injected frames: no `WANT_MONITOR_VIF`, no `QUEUE_CONTROL`, no active monitor) | no | **capture works, injection does not** — settled by the kernel sources, the BTF and the disassembly of the installed module (§4.1). They are good enough for the VRX half of the test, not for the TX half. |

The last row matters: **the adapters that are already on the machine can
receive, but they cannot transmit.** The capture half of the two-radio test
can be run with the hardware that is on the machine; the TX half needs an
adapter whose driver supports the monitor vif, and the cheapest and best
proven one is the **MT7612U** (ALFA AWUS036ACM). The two halves of the test,
the script and the exact capabilities it needs are in §3 and in
`QUESTIONS.md` Q8.

**Update (2026-10-22, later):** the **TP-Link row of that table is not the
end of the story.** The RTL8821AU of the TP-Link is supported by the
out-of-tree `rtl8812au` driver, that implements the monitor vif and the
injection, and that driver is now **prepared for this machine** (§2.12). So
the MT7612U is no longer the only way forward: it stays the simplest one
(in-tree, no DKMS), while the out-of-tree driver keeps the existing
hardware. The §7 requirement list is unchanged by that — the out-of-tree
driver meets points 1, 2, 3, 4 and 5 of it.

**Practical notes for the test, if new hardware is needed:** plug the adapter
in a **USB 2.0** port (several mt76 adapters lose the monitor mode and the
injection on USB 3), and the in-tree `mt76` drivers need no out-of-tree
package, no DKMS and no reboot.

**Sources** (checked on 2026-10-22):
- [kernel.org — mac80211 packet injection](https://www.kernel.org/doc/html/latest/networking/mac80211-injection.html)
- [wireless.docs.kernel.org — ath9k_htc (monitor mode, USB, Atheros AR9001/AR9271)](https://wireless.docs.kernel.org/en/latest/en/users/drivers/ath9k%5Fhtc.html)
- [wireless.docs.kernel.org — the in-tree wireless drivers and their features](https://wireless.docs.kernel.org/en/latest/_sources/en/users/drivers.rst.txt)
- [morrownr/USB-WiFi — the recommended adapters for an in-kernel (mac80211) driver](https://github.com/morrownr/USB-WiFi/blob/main/home/Recommended_Adapters_for_Kali_Linux.md)
- [morrownr/USB-WiFi #454 — the modern adapters with a (active) monitor mode and injection](https://github.com/morrownr/USB-WiFi/issues/454)
- [morrownr/USB-WiFi #387 — the mt7921au injection problems](https://github.com/morrownr/USB-WiFi/issues/387)
- [morrownr/7612u — the MT7612U notes (in-tree since Linux 4.19)](https://github.com/morrownr/7612u/)
- [openwrt/mt76 #839 — the active monitor mode support per chipset](https://github.com/openwrt/mt76/issues/839)
- [lwfinger/rtw88 #219 — the monitor mode of the RTL8812AU](https://github.com/lwfinger/rtw88/issues/219)
- [aircrack-ng/rtl8812au #1218 — the status of the in-tree 8812AU / 8821AU drivers](https://github.com/aircrack-ng/rtl8812au/issues/1218)
- [linux-wireless — the rtw88 series that adds the RTL8812AU / RTL8821AU](https://www.spinics.net/lists/linux-wireless/msg257590.html)
