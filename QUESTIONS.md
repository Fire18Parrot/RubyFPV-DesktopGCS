# RubyFPV — Desktop Port: Open Questions

Repository root: `/mnt/Giochi-e-Altro-2/RubyFPV/RubyFPV`.
Updated: 2026-10-22 (after the decisions of the user and after the video path
work, same day; Q9 added at the end of the day, after the retransmission
loop was closed).

The decisions of the user are recorded in the **Decision** line of each
question. The questions that are still open are the ones that need a fact the
port cannot establish by itself, or a new approval.

---

## Q1 — A monitor-mode-capable radio adapter (blocking live RF work)

**Status:** decided — **option B** (a separate, compatible USB adapter).
**Work done:** the research of the candidate chipsets and drivers is in
`STATUS.md` §7, with the exact requirements the adapter has to meet. No
driver was installed, no kernel was changed, no radio was reconfigured.
**Update (2026-10-22, later in the day):** the conclusion that the in-tree
rtw88 driver "cannot work at any privilege level" was based on the absence of
`monitor_start` / `monitor_stop` in `rtw88_core.ko`, and that argument turned
out to be **not decisive**: upstream rtw88 implements the monitor vif through
`IEEE80211_HW_WANT_MONITOR_VIF` and a `NL80211_IFTYPE_MONITOR` case in
`rtw_ops_add_interface()`, which adds no new symbol, and the in-tree support
landed in kernel 6.13 / 6.14 (this machine runs 7.2.11). The two-radio test
(Q8) will settle it on the real hardware; **if it works, no new adapter is
needed at all.** The research of §7 is the plan B.

**Update (2026-10-22, end of the day):** the question is now **settled by
the sources of the running kernel**, and the answer is more precise than
"it works / it does not work":

- the **capture** of the two rtw88 radios **works** (mac80211 provides the
  monitor interface by itself — the software monitor — and the driver is
  never asked to add a monitor vif);
- the **injection** of the two rtw88 radios **does not work**: the driver
  sets neither `IEEE80211_HW_WANT_MONITOR_VIF` nor
  `IEEE80211_HW_QUEUE_CONTROL` and it has no active monitor, so mac80211
  drops the injected frames before they reach the radio (the details and the
  evidence are in `STATUS.md` §4.1).

So the adapter that is still missing is **not** a "monitor capable" one: it
is an adapter whose driver supports the **monitor vif** (the injection).
The cheapest and the best proven one is the **MT7612U** (ALFA AWUS036ACM,
`mt76x2u`, in-tree, injection verified at all MCS levels). The two radios
that are on the machine stay useful for the **VRX** half of the two-radio
test.

**Update (2026-10-22, after the VRX half of the two-radio test was run):**
the two halves are now settled on the **real hardware**:
- the **capture** of the Eachine **works with the production code**: the
  monitor interface was created, the production filter was installed on it
  and the production parsing ran on the real frames of the air
  (`STATUS.md` §2.10);
- the **injection** of the TP-Link **cannot work**, and not only because of
  the sources of the kernel: the **loaded** `rtw88_core.ko` of the running
  kernel was disassembled and `rtw_ops_add_interface()` handles only the
  vif types 1, 2, 3 and 7 and reaches an `ud2` for every other one, and
  `NL80211_IFTYPE_MONITOR` is 6 (`STATUS.md` §4.1). **The TX half was not
  run**, because its result was already known and it would have been a
  privileged action with no possible outcome.

**The adapter to acquire is therefore the MT7612U** (or the cheaper
MT7610U), and it is the **third** adapter: the TP-Link stays the intended
transmitter, the Eachine the intended receiver.

The only RTL8812AU on this machine is driven by the in-tree `rtw88_8812au`
driver: it can **receive**, it cannot **transmit** (`STATUS.md` §4.1).
RubyFPV's 802.11 RX/TX is pcap-based and needs a monitor mode interface.

The alternatives are:

| Option | What it needs | Cost / risk | State |
|---|---|---|---|
| **A. Out-of-tree driver** `aircrack-ng/rtl8812au` (DKMS) for the existing USB card | installing a kernel module package, root | touches the kernel; the card must not be used for 2.4 GHz (5 GHz-only antennas) | **chosen** (the decision of the user changed on 2026-10-22, at the end of the day): the investigation and the port are done (`STATUS.md` §2.12), the build, the installation and the switch wait for the approval of Q10. **No reboot is needed.** |
| **B. A different USB adapter** with monitor mode in the in-tree driver | hardware only | none to the system; the safest option | **chosen** — and it is now specified more precisely: the driver must support the **monitor vif**, i.e. the **MT7612U / MT7610U** (mt76) |
| **C. A SiK or ELRS radio on USB** | hardware only | no RF capture at all | **not a substitute** for the 802.11 pcap path: the project supports the serial radio protocol as a complete alternative only for the radio link, not as a replacement of the 802.11 video path |

**Decision (user, 2026-10-22):** prepare for a separate, compatible USB
adapter. Research the exact chipsets and the Linux drivers, **with evidence
that they support the packet-capture behaviour RubyFPV actually needs** — a
chipset name alone does not guarantee compatibility. Do not install a DKMS
driver, do not modify the kernel and do not reboot for this project without
asking first. Option C is not a substitute for the 802.11 pcap path. Keep the
existing serial-radio implementation, but do not expand the SiK support during
this phase. Continue the software-side port work while the hardware is
unavailable.

Until the adapter is available, **live RF reception stays unvalidated** and
the system must not be described as flight-ready.

**Update (2026-10-22, last update of the day) — the decision changed: option
A is now the chosen one.** The user asked to stop treating a third adapter as
the default solution and to investigate an out-of-tree driver for the
**existing** TP-Link Archer T2U Plus (RTL8821AU, USB `2357:0120`), so that
the intended architecture is kept:

- the TP-Link `wlan2`: **TX/VTX**, with the out-of-tree driver, only while
  the RubyFPV radio function needs it;
- the Eachine `wlan1`: **RX/VRX**, with the working in-tree driver;
- the Intel `wlan0`: **never touched**.

The investigation is done and its result is positive: the
`aircrack-ng/rtl8812au` driver (branch `v5.6.4.2`, commit `7344855`) has the
exact USB id of the TP-Link in its device table, it implements the monitor
vif and the radiotap injection that the production TX code uses, and it is
already known to the RubyFPV code (the project loads `88XXau` and maps the
`rtl8812au` driver name to the RTL8812AU radio). The work is in
`code/radio/driver_88xxau/` and it is summarised in `STATUS.md` §2.12.

The consequences of the change of decision:

- **the MT7612U is no longer the only way forward.** It stays the simplest
  option (in-tree driver, no DKMS, no reboot) and it is still the
  recommended purchase, but the out-of-tree driver keeps the hardware that
  is already on the machine;
- the out-of-tree driver **has to be ported to the 7.2.11 kernel**: the
  released driver does not compile as it is. The port is written
  (`code/radio/driver_88xxau/88xxau-linux-7.2.patch`) and it applies cleanly
  to a pristine clone, but **it has never been compiled**;
- **no reboot is needed**: Secure Boot is disabled and the lockdown is
  `none`, so the unsigned DKMS module is accepted by this kernel;
- the switch is **reversible** and it is scripted both ways
  (`use_out_of_tree_driver.sh` / `restore_in_tree_driver.sh`), with the
  adapter identified by its USB hardware ID and with a refusal to touch the
  interface that carries the default route of the machine.

**The approval for the build, the installation and the switch is asked in
Q10.**

---

## Q2 — The 2.4 GHz link cannot be tested with the hardware present

**Status:** decided — 5.8 GHz is the reference band for the software work.

`wlan1` is the only Ruby radio on this machine and it is a model 7
"RTL8812AU DualAnt" with **5 GHz-only antennas**. The software guard refuses
to tune it to 2.4 GHz and that guard was not weakened. So the 2.4 GHz part of
the radio support (the default RubyFPV link is 2.4 GHz) cannot be exercised
with the current hardware.

**Decision (user, 2026-10-22):** use 5.8 GHz as the initial reference band
for the software integration, but **do not claim that 2.4 GHz support has been
validated**. Do not weaken the antenna safety guard and do not tune the
existing adapter to 2.4 GHz. Document the separate 2.4 GHz hardware
requirement. Any adapter recommendation must explicitly support the required
bands and RubyFPV's packet-capture needs.

---

## Q3 — Where should the desktop configuration live?

**Status:** decided and **implemented** — `~/.config/rubyfpv/`.

The desktop build used to keep the configuration in `/tmp/ruby/config/`,
mirroring the embedded layout (`/home/pi/ruby/config/`). Consequences: the
settings were **lost on reboot**, and `/tmp` is not a place a user expects to
find a configuration.

**Decision (user, 2026-10-22):** yes, move the desktop user configuration to
`~/.config/rubyfpv/`. Keep `/tmp/ruby/` for the runtime-only data (the FIFOs,
the temporary media, the logs). Preserve the embedded configuration layout.
Inspect all the `FOLDER_*` definitions and their consumers before changing
the paths. Migrate the existing desktop settings safely, without deleting or
overwriting the configuration. Add a regression test that shows that the
configuration survives a restart of the application and is stored
persistently.

**Done:** `code/base/config_paths.c` / `.h` (new), the `FOLDER_*` macros of
`code/base/config_file_names.h` are now runtime-resolved accessors, the
migration runs at the first start of `ruby_start`, and
`code/r_tests/test_config_persistence.cpp` (20 checks) covers it. See
`STATUS.md` §2.2.

---

## Q4 — Should the application be started automatically on the desktop?

**Status:** decided — **deferred**.

On the embedded boards a systemd unit starts RubyFPV at boot. On this
desktop the application is started by hand (`./ruby_start`).

**Decision (user, 2026-10-22):** defer the automatic startup until the core
port is stable. First make the manual startup/shutdown reliable and document
them. A user-level systemd unit may be investigated, but it must not be
enabled automatically, no system-wide service may be installed and no service
may start the application unexpectedly. If it is implemented, it stays
optional and the documentation explains how to enable, start, stop and disable
it.

**Done:** the manual start/stop is reliable and documented
(`./ruby_start`, `./ruby_start -stop`; the shutdown of the UI is graceful and
leaves no orphan process). No systemd unit has been created.

---

## Q5 — Scope of the vehicle side on the desktop

**Status:** decided — the ground station is the target.

The port covers the **ground station** side. The vehicle-side processes
(`ruby_rt_vehicle`, `ruby_tx_telemetry`, the camera capture, `ruby_i2c`) are
compiled but are not exercised on this machine: there is no flight
controller, no camera and no I2C bus attached, and the mission forbids
sending live control commands.

**Decision (user, 2026-10-22):** the ground-station operation is the primary
desktop target. The vehicle-side processes should compile and stay compatible,
but they must not be run against non-existent hardware and no desktop vehicle
simulation may be invented unless a focused regression test needs it.
Prioritize the real control-link integration, the telemetry handling, the
configuration and the video reception of the ground station. Clearly document
which functions require the embedded vehicle hardware.

**Done:** the vehicle side still compiles for the desktop; the ground station
is the part that is started and tested. The list of the functions that need
the vehicle hardware is in `STATUS.md` §5.

---

## Q6 — The two remaining `EPERM` messages at startup

**Status:** decided and **implemented**.

On every start the radio initialisation tried to set the raw TX power of
`wlan1` and got `Operation not permitted`, because that needs
`CAP_NET_ADMIN` and the application is not run as root (by rule).

**Decision (user, 2026-10-22):** improve the behaviour if it can be done
safely and accurately. Check whether the required capability is available
before attempting a privileged radio configuration. If it is not available,
skip the operation and log a clear informational message. Do not silently
claim success, do not hide a genuine failure and do not change the privileges
to avoid the messages. The radio must remain correctly reported as
unconfigured.

**Done:** `hw_process_has_net_admin_capability()` (`code/base/hardware_procs.cpp`)
reads `CapEff` from `/proc/self/status`; the radio configuration is skipped
with an explicit message when the capability is missing, the "radios
configured" marker is not written and the radio is reported as **not
configured** (the OSD shows "Not Configured", not "Set Freq Failed"). A start
now logs **0 soft errors**.

---

## Q7 — The stray fixture process from an earlier session

**Status:** decided and **done**.

PID 129064 (`python3 /tmp/ruby_rf_fixtures_quarantine/sik_emu.py`) was still
running from an earlier session. It was a **protocol fixture** (a SiK
emulator on a PTY), not evidence of radio support, and the in-repo test
`code/r_tests/test_sik_serial_path.cpp` supersedes it.

**Decision (user, 2026-10-22):** stop it only if it is verified to be the
exact fixture described and not a reused PID. Verify the command line and the
identity first. If it is the obsolete fixture, terminate only that process
gracefully. Do not kill unrelated Python processes and do not broadly clean
the temporary files.

**Done:** the process was verified (cmdline, executable, start time, the
`emu.pid` file), then it was terminated with SIGTERM. The unrelated
`python3 -q` process and the fixture files were left untouched.

---

## Q8 — The two-radio RF test: the approval for the privileged action

**Status:** **the approval was given and the VRX half was run** (2026-10-22,
the results in `STATUS.md` §2.10). The **TX half was not run**: the driver
of the TP-Link cannot inject the frames RubyFPV needs (§4.1 of `STATUS.md`),
and running it would have been a privileged action with no possible result.
**No further privileged action is pending.**

**What the test is.** The last unproven step of the primary objective: the
video stream travelling **over the air** between the two USB radios, through
the production receive path, into the desktop player.

- **TX:** the TP-Link Archer T2U Plus (USB 2357:0120) sends the real video
  stream, in real RF frames, built by the production TX code
  (`radio_build_new_raw_ieee_packet()` + `radio_write_raw_ieee_packet()`,
  i.e. a real `pcap_inject()`).
- **RX:** the Eachine WiFiLink (USB 0bda:881a) receives them with the
  production RX code (`radio_open_interface_for_read()`,
  `radio_process_wlan_data_in()`, `packet_process_and_check()`) and hands them
  over to `process_received_video_component_packet()`, so the video reaches
  the desktop player through the production video pipeline.

**What it needs, exactly:**
- **root** (or `CAP_NET_ADMIN` + `CAP_NET_RAW`): the monitor mode interfaces
  and the channel (`CAP_NET_ADMIN`) and the pcap capture / injection
  (`CAP_NET_RAW`). The application itself is **not** run as root — the desktop
  player is started as the user of the session.
- **a monitor mode interface on each radio**: `iw dev wlan2 interface add
  rubyvtx0 type monitor` and the same for `wlan1`.
- **one channel in the 5.8 GHz band** on both (149 by default). The Eachine
  has 5 GHz-only antennas, so the script refuses any 2.4 GHz channel and the
  software guard is not weakened.

**What the script does now, in this order (it was rewritten with the settled
kernel semantics):**
1. it finds the two radios by their **USB hardware id** and it refuses
   `wlan0` and any interface that carries the default route;
2. it refuses a channel below 32 (2.4 GHz);
3. it brings the **managed** interface of the radio **down** (the kernel
   refuses to change the channel of a monitor interface while another
   interface of the same radio is up) and it **moves the radio to the 5.8 GHz
   channel before the monitor interface is created**, so that the monitor
   interface is born on the right band;
4. it creates the monitor interface, sets the channel on it and then
   **reads the frequency back and aborts (deleting the monitor) if it is
   below 5000 MHz** — the Eachine must never stay on a 2.4 GHz channel;
5. it checks that the monitor interface really receives the frames of the air
   (the RX counters of the interface);
6. it starts the desktop player **as the user of the session** (after a
   `notify-send`), never as root;
7. it runs the **VRX half** against the live air (the production filter is
   installed on the live monitor interface and the production parsing runs
   on it). The **TX half is opt-in** (`--tx`), because the injection cannot
   work with these two adapters (§4.1 of `STATUS.md`): the script says so
   before it starts it, so that the result is not mistaken for a fault of
   the production code;
8. it restores the managed interfaces and deletes the monitor interfaces, also
   on an error or a Ctrl-C. The radios are left on the 5.8 GHz channel on
   purpose: it is the only band that the antennas of the Eachine support.

**What it changes on the machine, and how it is undone:**
- it creates **two new virtual interfaces** (`rubyvtx0`, `rubyvrx0`) on the two
  USB radios and deletes them at the end (also on an error or a Ctrl-C);
- it sets the channel of the two radios to the 5.8 GHz channel 149 — the
  radios are dedicated RubyFPV adapters, they are not associated with any
  network and they are not the network connection of the machine;
- it starts the desktop player (as the user) and stops it at the end;
- it does **not** touch `wlan0` (the Intel AX210 that carries the network
  connection): the script identifies the radios by their **USB hardware id**,
  it refuses `wlan0` by name and it refuses any interface that carries the
  default route, and the test program refuses `wlan0` too;
- it does not install a driver, does not change a kernel module, does not
  change any system-wide configuration, does not reboot and does not touch
  the persistent configuration in `~/.config/rubyfpv/`.

**How to run it (the command that was run):**
```bash
cd /mnt/Giochi-e-Altro-2/RubyFPV/RubyFPV
sudo -E ./code/r_tests/two_radio_rf_test.sh 60 149    # the VRX half against the live air
# sudo -E ./code/r_tests/two_radio_rf_test.sh --tx 60 149   # NOT run: the injection cannot work with these adapters
```

**What happened (the outcome, as it is).**
1. **The monitor interface was created and it received the frames of the
   air** — the outcome 1 below. The capture of the rtw88 radios works, as
   the kernel sources say it should, and the VRX half ran the production
   code on the live interface: the production filter was installed and the
   production parsing ran on the frames of the air. **The four frames that
   were on the air were ordinary Wi-Fi traffic and the production filter
   rejected every one of them**, which is the correct behaviour: no RubyFPV
   transmitter was on the air, so **no video was received and the player had
   nothing to display**. The capture half of the RF path is proven; the
   video reception over RF is **not**.
2. **The run also exposed a defect of the production code** — the radio
   enumeration mis-read the phy of an interface when a monitor interface was
   on the same radio, and the Eachine was recorded with **no supported band
   at all**, which makes a radio unusable. It is **fixed**, with a
   regression test (`STATUS.md` §2.11).
3. The **TX half was not run** (outcome 3 below, settled in advance): the
   disassembly of the loaded `rtw88_core.ko` shows that `rtw_ops_add_
   interface()` handles only the vif types 1, 2, 3 and 7 and reaches an
   `ud2` for every other one, and `NL80211_IFTYPE_MONITOR` is 6. The rtw88
   drivers cannot own a monitor vif, so mac80211 drops the injected frames
   before they reach the radio.

**Decision needed:** none for the test itself. The remaining decision is the
one of Q9: the **TX half** needs an adapter whose driver supports the
monitor vif (the **MT7612U**), and that is a purchase.


---

## Q9 — The software side of the video path is closed: what is the next step?

**Status:** new — a question of priority. No permission is asked for here.

**What has changed (2026-10-22, end of the day).** The video path is now
closed **at the software level**, with the **production code of both ends**:
- the video stream is received, reassembled and **displayed in the desktop
  application** (the player rendered the frames of the clip on the screen);
- the **forwarding** to a second player is verified byte for byte over UDP;
- the **retransmission loop** runs end to end: the station loses a video
  packet, the production `ProcessorRxVideo` builds the request, the
  **production vehicle code** answers it, and the video of the frame that had
  the lost packet reaches the desktop player **byte for byte**
  (`STATUS.md` §2.8 — 4 steps, 0 failures);
- the loop was then repeated **10 times in a row with the real desktop
  player**: 10 packets lost, 10 requests, 10 answers, **209600 bytes** and
  **80 frames decoded** by `ruby_player_desktop`
  (`STATUS.md` §2.9, `code/r_tests/video_retransmission_live_test.sh`).

**What is still missing is only the RF hop.**
- The two radios of this machine can **receive** but not **transmit**
  (`STATUS.md` §4.1): the disassembly of the loaded `rtw88_core.ko` shows
  that the driver handles only the vif types 1, 2, 3 and 7 and reaches an
  `ud2` for every other one, and `NL80211_IFTYPE_MONITOR` is 6. The driver
  cannot own a monitor vif, so mac80211 drops the injected frames: the
  **TX half** of the two-radio test **cannot succeed with them**, whatever
  the approval.
- The **VRX half was run** (Q8, `STATUS.md` §2.10): the capture of the live
  air with the production code works, the production filter rejected the
  four ordinary Wi-Fi frames that were on the air (there was no RubyFPV
  transmitter), so **no video was received**. The capture half is proven;
  the video reception over RF is not.
- The run also **found a real defect of the production code** — the radio
  enumeration mis-read the phy of an interface when a monitor interface was
  on the same radio, and the radio was then recorded with no supported band
  at all, which makes it unusable. It is **fixed**, with a regression test
  (`STATUS.md` §2.11).
- The **TX half** needs an adapter whose driver supports the **monitor vif**
  (option **B** of Q1: the **MT7612U**), or the out-of-tree driver (option
  **A** of Q1, rejected by the user).

**The decision needed — one single decision is left.**

1. ~~**Approve Q8** and run the **VRX half** against the live air.~~ **Done**
   (Q8): the capture half of the RF path is proven with the hardware that is
   on the machine, and the only thing left is the TX half.
2. **Acquire an MT7612U** (option B of Q1, hardware only, no DKMS, no
   reboot, no kernel change): the **full** two-radio loopback becomes
   possible, and with it the RF milestone and the hardware integration
   milestone. **This is the only thing that is left to decide.** The
   evidence that the exact adapter supports what RubyFPV needs (in-tree
   `mt76x2u` driver, monitor vif, injection at every MCS level, 2.4 and
   5.8 GHz) is in `STATUS.md` §7.
3. ~~**Continue with the work that needs no hardware.~~ **Done**: the
   sustained version of the loop is complete (`STATUS.md` §2.9: 10
   iterations, 10 losses, 10 requests, 10 answers, 209600 bytes and **80
   frames decoded** by the real player) and the run of the VRX half has
   already produced the one concrete defect of the RF hop that was still
   open, which is now fixed.

**What I recommend:** 2 — the purchase of the **MT7612U** is the only thing
that unblocks the **TX half**; the roles stay as they are (the TP-Link is
the intended transmitter, the Eachine the intended receiver, no swap) and
the MT7612U is the **third** adapter, the one that is only used for the
injection.
**There is no more software work on the video path that does not need the
hardware**, so the RF milestone is now the only thing that is missing.

---

## Q10 — The out-of-tree driver: the approval for the build, the installation and the switch

**Status:** step 1 (**the compilation**) is **done and it succeeded**; steps
2, 3 and 4 (the DKMS installation, the switch, the restore) are still
waiting for the approval of the user. **Nothing of the module was installed
or loaded, and no frame was ever transmitted with it.**

### What is ready

Everything is in `code/radio/driver_88xxau/`:

| File | What it is |
|---|---|
| `README.md` | the full investigation: the driver, the evidence, the kernel port, the Secure Boot answer, the plan |
| `88xxau-linux-7.2.patch` | the port of the driver to the Linux 7.x kernel API (3 files, 173 insertions) |
| `common.sh` | the shared definitions and the safety helpers |
| `prepare_source.sh` | clone at the pinned commit and apply the patch (no root) |
| `build_module.sh` | compile the module, install nothing (no root) |
| `install_module.sh` | DKMS add / build / install (root) |
| `use_out_of_tree_driver.sh` | switch the adapter to the out-of-tree driver (root) |
| `restore_in_tree_driver.sh` | bring the in-tree driver back (root) |
| `remove_module.sh` | remove the out-of-tree driver from the system (root) |

`prepare_source.sh` **has already been run** (it changes nothing outside
`/tmp/ruby/driver_88xxau/`): the clone is at the pinned commit and the
patch applies cleanly, and the patched tree is byte for byte the tree of the
hand-made port.

**`build_module.sh` has also been run, and it succeeded.** The module was
built for the running kernel and the result is the evidence:

```
$ modinfo /tmp/ruby/driver_88xxau/rtl8812au/88XXau.ko
name:           88XXau
vermagic:       7.2.11-1-cachyos SMP preempt mod_unload
alias:          usb:v2357p0120d*dc*dsc*dp*ic*isc*ip*in*     <- the TP-Link
parm:           rtw_monitor_disable_1m:Disable default 1Mbps rate for monitor injected frames (int)
```

(the module carries 103 parameters and the whole device table of the
driver). The build log is `/tmp/ruby/driver_88xxau/build.log` and it ends
with the normal banner of the driver, with no error. **Nothing was
installed**: the module is in `/tmp/ruby/driver_88xxau/`, it is not in
`/lib/modules`, it is not loaded, and the running driver of the TP-Link is
still the in-tree `rtw88_8821au`.

### The exact commands and their expected effects

**1 — compile only (no root, nothing is installed, the running driver is not
touched)**

```bash
cd /mnt/Giochi-e-Altro-2/RubyFPV/RubyFPV/code/radio/driver_88xxau
./build_module.sh
```

*Expected effect*: `make -j$(nproc) KSRC=/lib/modules/7.2.11-1-cachyos/build`
in `/tmp/ruby/driver_88xxau/rtl8812au`, that ends with the file
`88XXau.ko`. If the port is complete, this is the outcome. If something is
still missing, the script prints the remaining compile errors and the full
log is in `/tmp/ruby/driver_88xxau/build.log`. **This is the step that
proves the port.**

**2 — install with DKMS (root, changes the system)**

```bash
sudo ./install_module.sh
```

*Expected effect*: the source tree is copied to
`/usr/src/rubyfpv-88xxau-5.6.4.2-rubyfpv1`, DKMS registers the package
`rubyfpv-88xxau/5.6.4.2-rubyfpv1`, builds it for `7.2.11-1-cachyos` and
installs `/lib/modules/7.2.11-1-cachyos/updates/88XXau.ko`. **The running
driver is not changed by this step:** the module is installed but not loaded,
and the in-tree driver keeps working.

**3 — switch the adapter (root, changes the state of the radio)**

```bash
sudo ./use_out_of_tree_driver.sh
```

*Expected effect*: the TP-Link is identified by its USB hardware ID
`2357:0120`; the script refuses to run if that adapter carries the default
route of the machine (`wlan0`, the Intel AX210, which is never touched); the
adapter interface is brought down, the in-tree module `rtw88_8821au` is
unloaded and `88XXau` is loaded with `rtw_monitor_disable_1m=1` (mandatory
for the 5.8 GHz band: with the default value the driver forces the injected
frames to 1 Mbps and they would never reach the air). The adapter is briefly
absent and comes back, possibly with a **different interface name** (that is
why the radios are identified by their USB hardware ID). `wlan0` and `wlan1`
are not touched.

**4 — go back to the in-tree driver (root)**

```bash
sudo ./restore_in_tree_driver.sh
```

*Expected effect*: `88XXau` is unloaded, `rtw88_8821au` is loaded again, the
optional blacklist is removed. The out-of-tree module stays installed but it
is not loaded.

**5 — remove the out-of-tree driver from the system (root)**

```bash
sudo ./remove_module.sh
```

*Expected effect*: `dkms remove rubyfpv-88xxau/5.6.4.2-rubyfpv1 --all`, the
removal of `/usr/src/rubyfpv-88xxau-5.6.4.2-rubyfpv1`, the removal of the
optional blacklist and a `depmod -a`. The system is then exactly as it was
before this work.

### The optional persistent switch

`sudo ./use_out_of_tree_driver.sh --persist` also writes
`/etc/modprobe.d/rubyfpv-88xxau-blacklist.conf`, that blacklists
`rtw88_8821au`, so that the adapter always binds to the out-of-tree driver,
even after a reboot or after the adapter is plugged back in. It is a
system-wide configuration change, so it is **optional** and it is **not**
done by default.

### What is asked

1. ~~**Is step 1 (the compilation) approved?**~~ — **done, and it
   succeeded** (see above).
2. **Is step 2 (the DKMS installation) approved?** It puts a module file in
   `/lib/modules/7.2.11-1-cachyos/updates/` and it registers the package
   with `AUTOINSTALL="yes"`, so that the module is rebuilt at every kernel
   update. It does not change the running driver.
3. **Is step 3 (the switch) approved?** It changes the driver of the TP-Link
   only, and it is reversible with step 4. **No RF frame is transmitted by
   the switch itself**; the transmission would be a separate, later test,
   with its own approval.
4. **Should the persistent switch (the blacklist) be wanted?** It is the
   only system-wide configuration change of the whole plan.

### The constraints that stay in force

- `wlan0` (the Intel AX210, the network connection of the machine) is
  **never touched**: the scripts identify the adapter by its USB hardware
  ID and they refuse to act on the interface that carries the default route;
- **no RF transmission** during the driver phase: the switch only changes
  the driver, the air is not used;
- no package install, no kernel change, no reboot, no system-wide network
  configuration change, except the optional blacklist of the point 4 above;
- the antenna guard (`radio_cap_card_model_is_5ghz_only()`, model 7) is not
  touched, and the TP-Link (model 17, `CARD_MODEL_RTL8812AU_GENERIC`) is not
  restricted by it;
- the **worktree is preserved**: the changes are in `code/radio/driver_88xxau/`
  (new files) and in `code/base/hardware_radio.c` (one parameter added to
  the `modprobe 88XXau` calls), and nothing else was modified.

---

## Q11 — The capabilities of the ground station: the one command that has to be run by the user

**Status:** **RESOLVED for the installation** — the command was run by the
user and the capabilities are in place (`getcap` prints
`cap_net_admin,cap_net_raw=ep` on `ruby_start`, `ruby_controller`,
`ruby_central`, `ruby_rt_station`; the order of the steps of the script was
fixed first, so the kernel no longer drops them). **The second half is a
code defect, not a missing privilege** — see Q12 and `STATUS.md` §2.14.2:
the *process* had the capabilities, but the helper it starts (`iw`) did not,
because a child process does not inherit the effective set at `execve()`. The
fix is in the code (the capabilities are raised as **ambient**, so the helper
inherits them) and it is built, but it is **not live yet** (Q12).

### Why it is needed

The ground station has to change the settings of its radios — it puts them in
monitor mode, it tunes them, it sets their power — and it has to capture the
frames of the air with libpcap. Those operations need `CAP_NET_ADMIN` and
`CAP_NET_RAW`. The application is **never run as root**: the capabilities are
granted to the **files**, in a runtime directory, and the kernel gives them
to the process when it executes them. This is what the user asked for: *"i
want to be able to change network settings in rubyfpv. Give it permissions"*.

Without them the radio half of the pairing search cannot work, and the log
says exactly why:

```
[HW-R] Unexpected result: [RTNETLINK answers: Operation not permitted]
Router: Attempt to create packet socket failed - CAP_NET_RAW may be required
```
### The command

```bash
pkexec /mnt/Giochi-e-Altro-2/RubyFPV/RubyFPV/code/radio/install_gcs_capabilities.sh
```

It copies `ruby_start`, `ruby_controller`, `ruby_central`, `ruby_rt_station`,
`ruby_logger` and `ruby_player_desktop` into
`~/.local/share/rubyfpv/run`, gives `cap_net_admin,cap_net_raw=ep` to the
first four (the logger and the player get none), and makes the `res`
symbolic link. The project itself is on a **nosuid** mount, where the kernel
ignores the capabilities of a file, which is the reason of the runtime
directory. **The ground station has to be started from there**, with
`code/radio/run_gcs.sh`.

### The pitfall that was found, and that is fixed in the script

`chown()` **clears** the `security.capability` attribute of a file. The first
installation ran `setcap` and **then** changed the owner, so the script
printed the capabilities as installed and the binaries ended up with **none**
(`getcap` printed nothing). The script was reordered: the owner is changed
**first**, the capabilities are set **last**.

### How to check that it worked

```bash
getcap ~/.local/share/rubyfpv/run/*        # the four binaries must be listed
grep CapEff /proc/$(pgrep -f ruby_rt_station)/status   # the bits 12 and 13
```

### What is asked

1. **Can the command above be run?** It is the only privileged step of the
   radio work, it changes **nothing** outside the runtime directory (no
   system-wide permission change, no package, no service, no network
   configuration), and it can be undone with `setcap -r` on the same files.
2. **Which channel should the search use on the 5.8 GHz band?** The card of
   this machine has 5 GHz-only antennas, so the search has to run on the
   5.8 GHz band; **5825 MHz (ch 165)** is the channel that was chosen for the
   first run.

### The constraints that stay in force

- `wlan0` (the Intel AX210, the network connection of the machine) is
  **never touched**: the capability is not a reason to break the network, and
  `hardware_radio_is_protected_interface()` refuses every configuration
  operation on the interface that carries the default route;
- the antenna guard (`radio_cap_card_model_is_5ghz_only()`, model 7) is not
  touched;
- the application is not run as root and there is no unattended `sudo`;
- the capabilities are given to the **files** of the runtime directory only:
  no system-wide permission or capability change, no setuid binary.

---

## Q12 — The four fixes of Session 9 are built but not installed: the authorization for the run directory

**Status:** open — one explicit authorization of the user is needed, then the
live verification and the RF test can be done.

### What is fixed, built and green

Four defects of the running ground station were found in the log of the user's
own session and are fixed in the source (`STATUS.md` §2.14, `PROGRESS.md`
Session 9):

1. the false *"Vehicle OTA software update required"* that was also **hiding
   the video** (the version of the vehicle is known only as `11.8 (b-0)` from
   the telemetry);
2. the `iw` helper that lost `CAP_NET_ADMIN` (the capabilities are now
   raised as **ambient**, so the child inherits them — the exact `prctl`
   form was measured on this kernel and the first version of the fix, which
   the kernel accepted but that did nothing, was corrected; see `STATUS.md`
   §2.14.7);
3. the IPC storm on the channel `ROUTER-TO-TELEMETRY` that **nobody reads**
   on the desktop (5600 × `EAGAIN`, 30 ms lost per message);
4. the radio RX thread that burned a full core (the `POLLERR` / `POLLHUP` of
   the poll was never consumed) and the invisible pcap errors
   (`pcap_next()` → `pcap_next_ex()`).

The whole suite is green after a `make clean` and a full rebuild
(158 / 399 / 43 / 25 / 20 / 18 / 3 checks, 0 failures, `git diff --check`
clean). The new checks are `test_sw_version_compat` (the false "firmware
mismatch") and `test_ambient_caps` (the inheritance of the capabilities by
the helper process).

**The installation script does not have to change:** the measurement of the
ambient raise was made with a permitted set that contains only
`cap_net_raw` — exactly the file capabilities of the ground station — and it
succeeded, so **no `CAP_SETPCAP` is needed**.

### Why the authorization is needed

The ground station does **not** run from the repository: the repository is on
a **nosuid** mount, where the kernel ignores the capabilities of a file, so
the binaries are installed in a runtime directory:

```
~/.local/share/rubyfpv/run/   ruby_start, ruby_controller, ruby_central,
                               ruby_rt_station, ruby_logger, ruby_player_desktop
```

That directory still holds the **old** binaries. Copying the new ones there
overwrites **files outside the repository**, and it has to be done together
with the capabilities (`chown` **before** `setcap`, or the kernel drops them
again), so it is a privileged step. The agent is not allowed to run it on its
own.

### The exact commands, and what they do

```bash
# 1) the four binaries, from the build tree into the runtime directory
cp -f /mnt/Giochi-e-Altro-2/RubyFPV/RubyFPV/ruby_start          ~/.local/share/rubyfpv/run/
cp -f /mnt/Giochi-e-Altro-2/RubyFPV/RubyFPV/ruby_controller     ~/.local/share/rubyfpv/run/
cp -f /mnt/Giochi-e-Altro-2/RubyFPV/RubyFPV/ruby_central        ~/.local/share/rubyfpv/run/
cp -f /mnt/Giochi-e-Altro-2/RubyFPV/RubyFPV/ruby_rt_station     ~/.local/share/rubyfpv/run/

# 2) the capabilities again, on the copies (the user runs it, it asks for the password)
pkexec /mnt/Giochi-e-Altro-2/RubyFPV/RubyFPV/code/radio/install_gcs_capabilities.sh

# 3) the check
getcap ~/.local/share/rubyfpv/run/ruby_start ~/.local/share/rubyfpv/run/ruby_controller \
        ~/.local/share/rubyfpv/run/ruby_central ~/.local/share/rubyfpv/run/ruby_rt_station
# -> cap_net_admin,cap_net_raw=ep on all four

# 4) the ground station, started from the runtime directory
/mnt/Giochi-e-Altro-2/RubyFPV/RubyFPV/code/radio/run_gcs.sh
```

**Note on step 1:** `cp -f` writes **in place**, so the inode — and with it
the `security.capability` attribute — is kept. The capabilities are still
there after the copy, but step 2 makes it certain and is the one that is
asked for.

### What is asked

1. **May the four binaries of the runtime directory be overwritten with the
   newly built ones** (the command of step 1), and the capabilities be
   re-applied (step 2, with `pkexec`, so the password is typed by the user)?
   Nothing outside `~/.local/share/rubyfpv/run` is touched, and the old
   binaries are recoverable with a rebuild.
2. **The channel of the test:** 5825 MHz (ch 165) was the channel of the
   first run, on the 5.8 GHz band (the card of this machine has 5 GHz-only
   antennas; the 2.4 GHz guard is not touched).

### The constraints that stay in force

- `wlan0` is **never touched**;
- the antenna guard (model 7) is not weakened;
- the application is not run as root and there is no unattended `sudo`;
- no system-wide permission or capability change: the capabilities stay on
  the files of the runtime directory only;
- the VTX is **powered off** while the user is not in the room, so the test
  is a receive-only one until the user is back.

## Q-new-1 — GitHub publication approval and sandbox denial (blocking publication)

- **Relevant operation:** Phase 2 GitHub backup and repository publication.
- **Why a decision is needed:** The repository now has a verified local `old` branch and backup commit `70accb5639bfe8e49b28378d1ba2ec96cbca4d8d`, but publication was attempted and denied by the environment's safety classifier before the remote repository could be created. User stated approval, but the automated sandbox still blocks the action.
- **Available options:** 1. Run `gh repo create RubyFPV-DesktopGCS --private --source=. --remote=github --push` after the permission policy is relaxed. 2. Remove --source-based creation and manually configure the remote with `gh api`, still subject to the same policy. 3. Keep the backup local only.
- **Recommendation:** option 1, if the operator explicitly grants the policy exception; otherwise document the backup as local-only.
- **Blocks further work?** Yes for publishing and pushing; no for local investigation, auditing, builds, and tests.

## Q-new-2 — Audit scope vs project file mode changes

- **Relevant files:** all 1195 tracked files in the backup commit and build outputs.
- **Why a decision is needed:** The backup commit includes both small content edits and thousands of executable-permission mode changes. This makes the refactoring diff historically noisy and may obscure later real changes.
- **Available options:** 1. Leave repository metadata as-is and compare against commits. 2. Normalize file modes without changing content (potentially disruptive and unnecessary). 3. Add a `.gitignore` and reclassify generated files before future commits.
- **Recommendation:** option 1 for the backup branch; consider option 3 on `refactor` only if ignored files clearly need removal.
- **Blocks further work?** No.

## Q-new-3 — Live test evidence: RF video packets arrive but playback/UI still open/black

- **Relevant subsystem:** radio reception (`ruby_rt_station`), video RX processor (`code/r_station/processor_rx_video.cpp`), video buffers (`code/station/video_rx_buffers.cpp`), video output (`code/r_station/rx_video_output.cpp`), and desktop player (`ruby_player_desktop`).
- **Evidence from 2026-10-22 live test:** after the helper-capability fix, the latest log records `Start receiving radio packets from VID 2172108829`, then sustained `RX: VID 2172108829, Stream Video Stream 1: 551-571 packets/sec (~607k-630k bytes/sec)`, and opens a GStreamer pipeline: `appsrc ! decodebin ! videoconvert ! video sink`. A repeating message, `Flight controller telemetry missing`, is also displayed. The radio test proves VTX packets reach the station, but does not prove decoded/rendered frames.
- **Status:** open. Black video is not yet explained by the packet counters alone.
- **Available options / next diagnostics:** check whether frames are actually decoded and delivered, inspect player counters, and capture the decoder/output pipeline output.
- **Blocks further work?** No for code review; yes for declaring the fix complete.
