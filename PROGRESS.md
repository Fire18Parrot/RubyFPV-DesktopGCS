# RubyFPV — Desktop Port: Progress Log

Repository root: `/mnt/Giochi-e-Altro-2/RubyFPV/RubyFPV` (branch `main`).
Chronological record of the port work, what was changed, and how each change
was verified. Newest entry first within each date.

The scope and the priorities come from the mission "RubyFPV Overnight
Autonomous Port Completion": core application operation → radio → video →
vehicle control and telemetry → configuration/UI → reliability tests. The
constraints that stayed in force the whole time: the application is never run
as root, no unattended `sudo`, no system-wide permission or capability
changes, **no change to the Wi-Fi interface modes/channels/frequencies/
regulatory settings**, **no disruption of the existing network connection**
(`wlan0`), no arming of a vehicle, no motors, no live flight-control
commands, no flight, no package installs without approval, and no destructive
cleanup.

---

## 2026-10-22 — Session 9: the four defects of the running ground station, found in the user's own log — and the false "firmware mismatch"

### 1. The symptom, from the user

> "IT CONNECTED... however.... its complains that there is a firmware mismatch
> between the vtx and the vrx, but they are both supposedly on 11.8"

and the screen of the ground station:

> "Vehicle OTA software update required" / "Video protocols have changed. You
> must update your vehicle/controller" — "5825 Link-1" — "N/A 0 FPS" —
> "Video Frames, FPS: *0"

The vehicle's VTX was **powered off** during that run (the user was in
another room), so the run is a clean picture of what the ground station does
**with no transmitter on the air**. The log of that run
(`/tmp/ruby/logs/log_system.txt`) is the evidence for everything below.

### 2. The false "firmware mismatch" — and it was hiding the video

The OSD decides between "show the video" and "show the red background with
the OTA message" in `_render_video_background()`
(`code/r_central/ruby_central.cpp`): the video was shown only when the
version of the vehicle was **exactly** equal to the version of the controller
(`11.8` build `11801`, `code/base/base.h:36-38`).

The version of the vehicle comes from two places: the **telemetry**, where it
is one byte with 4+4 bits (major/minor only — so
`Model::populateFromVehicleTelemetryData_v6()`, `code/base/models.cpp:7097`,
can only rebuild `11.8` and **sets the build number to 0**: *"set sw version
to: 11.8 (b-0)"*), and the **full model settings sync**, which carries the
real build number. Both are in the log, for the same vehicle (VID 2172108829):

```
199 lines:  software: 11.8 (b-0)        <- the telemetry, the build is unknown
168 lines:  software: 11.8 (b-11801)   <- the truth, after the settings sync
```

So the vehicle really is 11.8 (build 11801 = 11.8.01): the complaint was an
artefact of the unknown build number, and it was **not** cosmetic — while the
build number is 0 the condition "the versions are equal" is false, so the
**video was not displayed at all**.

**The fix.** A new `_classify_vehicle_software_version()` in
`code/r_central/ruby_central.cpp` classifies the version of the vehicle as
*compatible* / *older* (the vehicle must be updated) / *newer* (the controller
must be updated) and compares the **major and minor** when the build number
is unknown, against `MIN_BUILD_FOR_CURRENT_VIDEO_PROTOCOL = 11503` (the first
vehicle build that speaks the current video protocol). All the OSD call sites
follow that classification. The build number is still used when it is known,
so a vehicle that really is too old is still told to update.

### 3. The radio could not be tuned: the helper program lost the capability

The log has **523** lines of

```
[HW-R] Unexpected result: [RTNETLINK answers: Operation not permitted]
```

from the `iw` helper that the production code runs to put the card in
monitor mode and to tune it.

**The capabilities were installed correctly** (`getcap` shows
`cap_net_admin,cap_net_raw=ep`), and the process itself had them — but the
**child process does not inherit them**: at `execve()` the effective set of
the child is empty, and only the capabilities that are in the **inheritable**
set and that are **raised as ambient** survive. The `iw` child therefore ran
without `CAP_NET_ADMIN` and every attempt to configure the radio failed.

**The fix.** `hardware_raise_ambient_capabilities()`
(`code/base/hardware.cpp`, declared in `code/base/hardware.h`) reads
`CapPrm` and `CapInh` from `/proc/self/status` and raises, with
`prctl(PR_CAP_AMBIENT_RAISE)`, every capability that is in both sets — the
kernel adds the permitted capabilities of a file to the inheritable set, so
`cap_net_admin` and `cap_net_raw` can be raised. It is called at the start of
`ruby_central` and `ruby_rt_station`, the two processes that configure the
radios. **No new privilege, nothing system-wide**: the process passes on
what it already has.

### 4. The IPC storm: the router wrote into a channel that nobody read

**5600** lines of

```
Router: SOFT_ERROR: [IPC] Failed to write to IPC [ROUTER-TO-TELEMETRY], error code: EAGAIN
Router: [IPC] Channel [ROUTER-TO-TELEMETRY] ... 84 pending messages, 16300 used bytes, max bytes ...: 16384
Router: SOFT_ERROR: Main loop took too long (max 13 ms, fps: 60): 21 ms
```

The channel was **full** and nobody read it: its reader is `ruby_rx_telemetry`,
that the desktop **never starts** (it handles the telemetry of a SiK / ELRS
radio; the telemetry of a Wi-Fi link travels on the radio link itself). Every
write failed, and the retry loop of the IPC write (`msgsnd(..., IPC_NOWAIT)`,
3 attempts, 10 ms of pause between them) **wasted 30 ms per message**: the
main loop of the router took 21 ms instead of 13 ms, forever.

**The fix.** In `open_pipes()` (`code/r_station/ruby_rt_station.cpp`) the
write endpoint of that channel is opened **only when there is a serial radio**
(`hardware_radio_has_serial_radios()`); on the desktop there is none.

### 5. The radio RX thread burned a full core

The router's radio RX thread (`code/radio/radio_rx.c`) was at **100 % CPU**
with the vehicle off, i.e. with **nothing to receive**. The thread polled the
capture descriptor and read the packet **only if the poll reported `POLLIN`**.
A poll can also report `POLLERR` / `POLLHUP` — the `ENOBUFS` of a full
receive buffer, that the kernel keeps pending until a read consumes it — and
then **no read was ever attempted**: the error was never consumed, the poll
returned immediately at every iteration and the thread spun.

**The fix** (`code/radio/radio_rx.c`): a read is attempted also on
`POLLERR`/`POLLHUP`, so the pending error is consumed and the poll blocks
again; when a poll wake-up produced **no packet at all** the thread waits
1 ms, so that no condition can make it spin. In `code/radio/radiolink.c`,
`radio_process_wlan_data_in()` uses **`pcap_next_ex()`** instead of
`pcap_next()`: `pcap_next()` returns `NULL` both when there is no packet and
when the read failed, and **the error was invisible**; `pcap_next_ex()`
distinguishes them (0 = no packet, < 0 = error) and the error is now logged
(every 10th, so a full buffer does not flood the log). A transient error does
**not** declare the interface broken — only a real failure of the interface
does.

### 6. The router died every ~56 s — the watchdog, and what is still open

The central watches the router (`code/r_central/link_watch.cpp`): if the
router does not report for more than 1.1 s, four times in a row, the central
kills it and starts it again. The log has **8** such restarts, one every
~56 s:

```
Central: SOFT_ERROR: Router process is about to fail. Last active time: 9283 ms ago, loop counter: 74114
Central: ERROR: Router process has failed. Current router PIDS: [738384].
```

The last line the router wrote before every death is always the same:

```
Router: Send pairing request to VID: 2172108829
```

and then it is silent for **~9.3 s**. The pairing request is the only thing
the router puts on the air while the vehicle is off.

**Proved and fixed:** the two proved causes of that silence are the EAGAIN
storm of §4 (30 ms lost per refused message) and the radio tuning that could
not run at all (§3). Both are fixed.

**Not proved:** the exact instruction in which the router blocks was **not**
identified. This machine has `ptrace_scope=1`, so no debugger can be
attached to the running process (`gdb -p` → "ptrace: Operation not
permitted", `eu-stack -p` → "Permission denied") and `/proc/<pid>/maps` is
not readable either. `pcap_inject()` on the blocking TX handle of a card that
cannot transmit is the prime suspect, but it is a **suspicion, not a
measurement**, and it is not claimed as a fact.

### 7. The build of the tree was not reproducible: stale object files

`test_sik_serial_path` failed with 2 checks (the packet was not delivered by
the production RX thread) although it had passed 25/25 the day before. The
cause was **not** in the code: the object file of
`code/base/hardware_radio_sik.c` was **newer than its source** but had been
built from an **intermediate state** of it, in which the file descriptor of
the SiK radio was not stored — the radio was "opened", the write worked, and
the RX thread polled **fd 0** (the standard input) instead of the serial
device. The Makefile has **no header dependency tracking** and cannot see
it, so a `make` that looks up to date silently keeps a stale object.

**The rule that follows:** before a build that has to be **installed** and
**run**, do `make clean` and build again. After the clean build of this
session every binary is built from the sources that are in the tree now, and
the whole suite is green:

| Test | Result |
|---|---|
| `test_video_rx_pipeline` | 158 / 0 |
| `code/r_tests/video_retransmission_loop_test.sh` | 4 steps, 38 checks, 0 failures |
| `test_sik_serial_path` | **25 / 0** (it was 23 / 2 with the stale object) |
| `test_radio_capabilities` | **399 / 0** |
| `test_menu_rf_profiles` | 43 / 0 |
| `test_config_persistence` | 20 / 0 |
| `git diff --check` | clean |

### 8. What is left for the user

The four fixes are **in the source and built**, but the run directory
(`~/.local/share/rubyfpv/run`) still holds the **old** binaries. Copying the
new ones there (and re-applying the capabilities) overwrites files outside
the repository and needs the explicit authorization of the user; until then
the fixes are not live and the RF test cannot be run.

### 9. The fix of the capabilities was itself wrong — the `prctl` form was measured

The fix of the "the helper lost the capability" defect (point 3 above) was
written with

```c
prctl(PR_CAP_AMBIENT_RAISE, (unsigned long)(uintptr_t)&uCapability, 0, 0, 0);
```

and the new regression test (`code/r_tests/test_ambient_caps.cpp`) showed
that **on this kernel the call returns 0 and does nothing**: the ambient set
stays empty, so the `iw` child still runs without `CAP_NET_ADMIN`. The test
caught a defect in my own fix, which is exactly what it is for.

`strace` is not installed, so the call that works was found by intercepting
`prctl` with an `LD_PRELOAD` shim while `capsh --addamb=cap_net_raw` runs:

```
[trace] prctl(opt=47, arg2=2, arg3=13/0xd, arg4=0, arg5=0) = 0
```

i.e. `prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, <capability number by
value>)`. The four candidate forms were measured in a user namespace, in the
exact state of the ground station (uid 1000, `CapPrm = CapInh = CapEff =
0x2000`):

| Form | Call | Result |
|---|---|---|
| A | `prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, &cap)` | `EINVAL` |
| B | `prctl(PR_CAP_AMBIENT_RAISE, &cap)` | 0, **no effect** |
| C | `prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, cap)` | **0, and it works** |
| D | `prctl(PR_CAP_AMBIENT_RAISE, cap)` | `EFAULT` |

The production code now uses form C. The measurement needs **no
`CAP_SETPCAP`** (the permitted set contained only `cap_net_raw`, exactly the
file capabilities of the ground station), so
`code/radio/install_gcs_capabilities.sh` does not have to change.

The regression test, run in a user namespace that has the capabilities of the
ground station (`cap_net_raw` + `cap_net_admin`):

```
  the capability sets of this process: permitted=0x3000 inheritable=0x3000 ambient=0x3000
  PASS  the ambient set was empty before the raise                   got=0x0
  the child process: ambient=0x3000 effective=0x3000 (it must contain 0x3000)
  PASS  the child process gets the capabilities as ambient           got=0x3000
  PASS  the child process inherits the capabilities                  got=0x3000
Tests: 3, failures: 0
```

The child process is an ordinary program without any file capability,
exactly like the `iw` helper of the ground station: it **does** inherit the
privileges.

### 10. The state of the suites after the clean rebuild

`make clean` + `RUBY_BUILD_ENV=desktop make -j20 all` → 0 errors, and:

| Test | Result |
|---|---|
| `test_video_rx_pipeline` | 158 / 0 |
| `test_radio_capabilities` | 399 / 0 |
| `test_menu_rf_profiles` | 43 / 0 |
| `test_sik_serial_path` | 25 / 0 |
| `test_config_persistence` | 20 / 0 |
| `test_sw_version_compat` | 18 / 0 |
| `test_ambient_caps` | 3 / 0 (and 3 / 0 with the capabilities, in the namespace) |
| `code/r_tests/video_retransmission_loop_test.sh` | 4 steps, 0 failures |
| `git diff --check` | clean |

---

## 2026-10-22 — Session 8: the pairing search that never progressed — the gate that hid the USB radio, fixed

### 1. The symptom, from the user

> "ok so i'm trying to pair my video transmitter with the receiver software
> but when i run search it never completes/progresses the search. Try looking
> at the logs"

The log of that run repeats, every ~80 ms and forever:

```
Central: MenuSearch::onSearchStep(): waiting for router to be ready...
```

and the same log contains the line that explains it:

```
Central: Desktop build: not starting the controller router process.
```

### 2. The root cause, read out of the code

The search step cannot advance before the router is ready: the router
(`ruby_rt_station`) is the only producer of
`PACKET_TYPE_LOCAL_CONTROLLER_ROUTER_READY`, the central stores its
timestamp in `g_RouterIsReadyTimestamp`, and `MenuSearch::onSearchStep()`
returns without advancing while that timestamp is 0
(`code/r_central/menu/menu_search.cpp`, substep 2).

The router was **never started**: the desktop build refuses to start it
unless there is a **serial** radio. There were two gates, both on
`hardware_radio_has_serial_radios()` (SiK / ELRS only,
`code/base/hardware_radio.c`):

| File | What it gates |
|---|---|
| `code/r_central/launchers_controller.cpp` → `controller_launch_router()` | the router itself |
| `code/r_start/ruby_start.cpp` → `bStationExpected` | the watchdog of the start process |

The Eachine is a **Wi-Fi** radio (`wlan1`, `rtw88_8812au`, card model 7) and
was already enumerated as a **supported** radio
(*"Found supported radio type: Realtek, driver: 8812au"*), but a Wi-Fi radio
is never a serial radio, so the gate never counted it. A deadlock: no router
→ no capture of the air → no RubyFPV frame → nothing to find, and the UI
waits for a router that is never started.

### 3. The fix

New predicate in `code/base/hardware_radio.c`, declared in
`code/base/hardware_radio.h`:

```c
int hardware_radio_has_usable_radios()
```

It answers the question the gate was really asking — *is there a radio that
this ground station can use?* — and counts:

- a **serial** radio (SiK / ELRS): no privilege needed;
- a **supported Wi-Fi card** that is **not** the interface that carries the
  network connection of the machine: it is put in monitor mode and captured
  with libpcap.

The network interface is excluded by the existing
`hardware_radio_is_protected_interface()`, that reads the **default route**
(`ip -o route show default` → `dev wlan0`), so `wlan0` is still never
touched, whatever the gate says.

Used in the two places that had the old gate. `controller_launch_rx_telemetry()`
keeps the **serial-only** condition on purpose: that process handles the
telemetry of a SiK / ELRS radio that the router forwards to it; the
telemetry of a Wi-Fi link is carried by the radio link itself.

Build: `RUBY_BUILD_ENV=desktop make -j24 station ruby_central` → 0 errors.
Regression suite: `test_video_rx_pipeline`, the whole retransmission loop
(`code/r_tests/video_retransmission_loop_test.sh` → 25 checks, 0 failures),
`test_sik_serial_path`, `test_radio_capabilities`, `test_menu_rf_profiles`,
`test_config_persistence` → all PASS.

**The gate, measured on this machine** (a program that links the production
`hardware_radio.c` and calls the two predicates):

```
radio interfaces found: 2
  1: wlan1  driver=rtw88_8812au  type=3 supported=1 serial=0 protected=0
  2: wlan0  driver=iwlwifi        type=0 supported=0 serial=0 protected=1
hardware_radio_has_serial_radios()   = 0     <- the old gate: the router never started
hardware_radio_has_usable_radios()   = 1     <- the new gate: the router starts
```

`wlan0` is **protected** (it carries the default route) and **unsupported**:
it is counted by neither predicate, so it is neither used nor touched.

**Two more fixes that this change made necessary.**

- `ruby_start.cpp`: the watchdog alarmed when the router process was not
  running. The router is started by the central, when a search or a
  connection starts, and not at boot: on the desktop the alarm would have
  been a false one at **every** start. The alarm is now raised only when the
  station **was** running and then stopped, and the log tells what is
  expected: *"A usable radio was found. The router/station process is started
  by the ground station when a search or a connection starts."*
- `ruby_rt_station.cpp`: outside the search mode the router works for one
  vehicle, and it dereferenced the current model without a check. On a
  desktop where no vehicle is selected that was a null dereference; the
  router now refuses to start, with a message, instead of crashing.

### 4. The second half of the problem: the privileges, and a pitfall

With the router able to start, the radio half is blocked by the missing
capabilities. The evidence of the previous radio run:

```
[HW-R] Executing command: iw dev wlan1 set type monitor
[HW-R] Unexpected result: [RTNETLINK answers: Operation not permitted]
Router: Attempt to create packet socket failed - CAP_NET_RAW may be required
```

The *"unknown encapsulation on [wlan1]"* line of that run is a **red
herring**: the production code logs the same text for a failed
`pcap_activate()`, whatever the reason of the failure is.

The user asked for the privileges explicitly: *"i want to be able to change
network settings in rubyfpv. Give it permissions"*. The design keeps the
application **away from root**:

- the binaries are copied to `~/.local/share/rubyfpv/run` and **that** copy
  is executed (`code/radio/run_gcs.sh`): the project is on a **nosuid** mount
  and the kernel ignores the capabilities of a file that is on such a mount;
- `setcap cap_net_admin,cap_net_raw=ep` on `ruby_start`, `ruby_controller`,
  `ruby_central`, `ruby_rt_station`; the logger and the player get none;
- `code/radio/install_gcs_capabilities.sh` does the copy and the `setcap`,
  and it needs the root privileges (only a process with `CAP_SETFCAP` can set
  a file capability).

**The pitfall, worth recording:** `chown()` **clears** the
`security.capability` attribute of a file. The first installation ran
`setcap` and then `chown`, so the script printed the capabilities as
installed and the binaries ended up with **none** (`getcap` prints nothing).
The script was reordered (owner first, capabilities last) and the
installation has to be run again.

**State at the end of the session:** the runtime directory holds the **new**
binaries (the gate fix is in them) and the `res` symbolic link, but
`getcap ~/.local/share/rubyfpv/run/*` prints **nothing** — the capabilities
are not effective until the installation is run again. The agent is not
allowed to run `pkexec` (the permission classifier refuses the grant of
`CAP_NET_ADMIN`/`CAP_NET_RAW`), so the command is left to the user:

```bash
pkexec /mnt/Giochi-e-Altro-2/RubyFPV/RubyFPV/code/radio/install_gcs_capabilities.sh
```

Then the checks are: `getcap ~/.local/share/rubyfpv/run/*` (the four
binaries must be listed) and the `CapEff` field of
`/proc/<pid>/status` of the running `ruby_rt_station` (the bits 12 and 13).

### 5. The search parameters, once the privileges are in place

The Eachine has **5 GHz-only antennas** (card model 7, the guard of
`radio_cap_card_model_is_5ghz_only()` was **not** weakened), so the search
has to run on the **5.8 GHz** band; the channel chosen by the user is
**5825 MHz (ch 165)**. The card is already registered in the controller
settings with the right flags (`controller_radio_interfaces.cfg`: MAC
`9A68F50BED7E`, model 7, flags 101 = `CAN_USE_FOR_VIDEO | CAN_USE_FOR_DATA
| CAN_RX`), it is not disabled and no RF profile restricts it.

### 6. A correction to the previous session's document

§2.12 of `STATUS.md` said that the out-of-tree driver *"has never been
compiled"*. That was wrong: the module **was** compiled for this kernel
during the previous session, and the evidence is in the module itself:

```
$ modinfo /tmp/ruby/driver_88xxau/rtl8812au/88XXau.ko
name:           88XXau
vermagic:       7.2.11-1-cachyos SMP preempt mod_unload
alias:          usb:v2357p0120d*dc*dsc*dp*ic*isc*ip*in*     <- the TP-Link
parm:           rtw_monitor_disable_1m:Disable default 1Mbps rate for monitor injected frames (int)
```

It has **never been installed nor loaded**, and no frame was ever
transmitted with it. `STATUS.md` §2.12 was corrected accordingly.

---

## 2026-10-22 — Session 7: the out-of-tree driver for the **existing** TP-Link (prepared, **not** installed)

The user changed the decision: a third adapter is no longer the default
solution. The TP-Link Archer T2U Plus (RTL8821AU, USB `2357:0120`) is to be
driven by a compatible out-of-tree driver, so that it can inject the RubyFPV
frames in monitor mode. The intended architecture is unchanged: the TP-Link
`wlan2` is the TX/VTX, the Eachine `wlan1` is the RX/VRX with the working
in-tree driver, the Intel `wlan0` is never touched.

### 1. The driver, and the evidence that it does what RubyFPV needs

`https://github.com/aircrack-ng/rtl8812au.git`, branch `v5.6.4.2`, pinned
commit `734485506a30d6237c2deaad666a19f8ca5379f2`. The module file is
`88XXau.ko`, the USB driver name is `rtl8812au`.

| Requirement | Where it is in the driver |
|---|---|
| the TP-Link itself | `os_dep/linux/usb_intf.c:208` — `{USB_DEVICE(0x2357, 0x0120), .driver_info = RTL8821}` |
| monitor mode with the radiotap link type | `Makefile:59` (`CONFIG_WIFI_MONITOR = y`), `os_dep/linux/ioctl_cfg80211.c:4936` (`ARPHRD_IEEE80211_RADIOTAP`) |
| **the injection** of a radiotap + 802.11 frame | `os_dep/linux/xmit_linux.c:547` → `core/rtw_xmit.c:4401` (`rtw_monitor_xmit_entry()`), and the monitor netdev path at `os_dep/linux/ioctl_cfg80211.c:4721` |
| the 5.8 GHz band | the parameter `rtw_monitor_disable_1m` (`os_dep/linux/os_intfs.c:87`): with the default the driver forces the injected frames to 1 Mbps, that does not exist on the 5 GHz channels → the module has to be loaded with `rtw_monitor_disable_1m=1` |

The production code does exactly that: `iw dev <if> set type monitor`
(`code/base/hardware_radio.c:2007` and `:2075`) and then `pcap_inject()` of a
radiotap + 802.11 DATA frame (`code/radio/radiolink.c`, the headers at `:105`
and `:124`).

A second, independent benefit: with the in-tree `rtw88_8821au` the TP-Link
is **not recognised at all** by the project (the driver string matches none
of the patterns of the enumeration); with the out-of-tree driver the uevent
reports `DRIVER=rtl8812au`, that the project maps to
`RADIO_HW_DRIVER_REALTEK_RTL8812AU`.

### 2. The port to the 7.2.11 kernel

The released driver does not compile as it is on `7.2.11-1-cachyos`. Every
break was identified against the local headers and every one of them is
fixed by the new file `code/radio/driver_88xxau/88xxau-linux-7.2.patch`
(3 files, 173 insertions, 17 deletions):

- the nine `cfg80211_ops` callbacks that now take a `struct wireless_dev *`
  instead of a `struct net_device *` (`add_key`, `get_key`, `del_key`,
  `set_default_mgmt_key`, `add_station`, `del_station`, `change_station`,
  `get_station`, `dump_station`);
- `cfg80211_new_sta()` and `cfg80211_del_sta()` (3 call sites);
- the `const u8 *rx_addr` of `remain_on_channel`;
- the `int radio_idx` of `set_wiphy_params`, `set_tx_power` and
  `get_tx_power` (kernel 6.17 and later);
- the removal of `strncpy()` from the kernel headers (a static inline with
  the same semantics);
- `from_timer()` → `timer_container_of()` and `del_timer_sync()` →
  `timer_delete_sync()`;
- the PPPoE structures, whose flexible array members are hidden from the
  kernel code.

The upstream pull requests **#1264** and **#1267** of the same repository
were used as the reference of the direction of the port; the patch itself
was written against the local headers, because the copy of the Buildroot
patch for the Linux 7.2 that circulates on the mailing list is mangled by
the RFC-3676 line wrapping.

### 3. Secure Boot and the module signing

The `SecureBoot` efivar is `0` and the `SetupMode` efivar is `1`: Secure
Boot is disabled. The lockdown is `[none]`, `dkms` is 3.4.4 and the kernel
headers are installed. **No MOK has to be enrolled and no reboot is needed**:
the unsigned module that DKMS builds is accepted by this kernel.

### 4. The scripts, in `code/radio/driver_88xxau/`

`common.sh` (the safety core), `88xxau-linux-7.2.patch`,
`prepare_source.sh`, `build_module.sh`, `install_module.sh`,
`use_out_of_tree_driver.sh`, `restore_in_tree_driver.sh`,
`remove_module.sh` and the `README.md` that holds the whole investigation.

The safety rules that every script that changes the module state obeys:

- the adapter is identified by its **USB hardware ID** (`2357:0120`), never
  by its interface name, because the name changes across a reload;
- the script **refuses to act** if that adapter is the interface that
  carries the default route of the machine (`wlan0`, the Intel AX210);
- the extra interfaces of the same adapter (the monitor ones) are removed
  before the module is unloaded;
- nothing is forced: if the in-tree module refuses to be unloaded, the
  script says so and asks for the adapter to be unplugged and plugged back.

### 5. The project change, and the verification

`code/base/hardware_radio.c`: the `modprobe 88XXau` / `insmod 88XXau.ko`
calls now carry `rtw_monitor_disable_1m=1`, that is mandatory for the
5.8 GHz band. The parameter is ignored by the drivers that do not have it
(modprobe passes the unknown parameters to the kernel, that ignores them
with a warning — verified with `modprobe --dry-run`).

- `git diff --check`: clean.
- The project rebuilds (`make station RUBY_BUILD_ENV=desktop`).
- The regression suite is green: `test_video_rx_pipeline` 158/0,
  `test_radio_capabilities` 402/0, `test_menu_rf_profiles` 43/0,
  `test_sik_serial_path` 25/0, `test_config_persistence` 20/0.
- `prepare_source.sh` has been run (it touches only
  `/tmp/ruby/driver_88xxau/`): the patch applies **cleanly** to a pristine
  clone, and the patched tree is byte for byte the tree of the hand-made
  port.

**What has not been done:** the driver has not been compiled, not
installed, not loaded, and **no RF frame has been transmitted**. The
approval for the compilation, the DKMS installation and the switch is asked
in `QUESTIONS.md` Q10.

---

## 2026-10-22 — Session 6: the two-radio RF test, the **VRX half** run against the live air — and a real defect of the production code, found and fixed

The user authorized the **receive-only** run of the two-radio RF test
("I authorize this specific test's temporary privileged interface and channel
changes on the two dedicated USB adapters only"). The **TX half was not
run**: the driver of the TP-Link cannot inject the frames RubyFPV needs
(§4.1 of `STATUS.md`), so running it would have been a pointless privileged
action.

### 1. The safety of the script, checked in full before the run

`code/r_tests/two_radio_rf_test.sh` was read in full and every command that
changes something was identified: `ip link set <radio> down`, `iw dev <radio>
set channel <ch>`, `iw dev <radio> interface add <mon> type monitor`, `ip
link set <mon> up`, `iw dev <mon> set channel <ch>`, and the cleanup
(`iw dev <mon> del`, `ip link del <mon>`, `ip link set <radio> up`). The five
safety points of the brief are all satisfied:

| Check | Where it is done |
|---|---|
| the adapters are identified by their **USB hardware id** | `netdev_for_usb_id()` (2357:0120 → `wlan2`, 0bda:881a → `wlan1`) |
| `wlan0` and the default route are **refused** | the explicit `wlan0` test and the `default_route_if()` test, before anything is changed |
| the 5 GHz-only antennas are respected | a channel below 32 is refused, and the frequency of the monitor interface is read back and checked against 5000 MHz |
| the state is restored, also on an error or a Ctrl-C | `trap cleanup EXIT INT TERM` |
| no driver, no kernel module, no reboot, no system-wide configuration | the script only runs `iw`, `ip` and the test binary |

### 2. The first run: the harness did not work, and it said so wrongly

The first run failed, and the failure was **not** the production code:

- the production RX code refused to open the live monitor interface
  ("Tried to open a non 2.4/5.8 radio in high capacity 2.4/5.8 mode"). The
  cause was in the test, not in the product: the production code enumerates
  the radio interfaces the first time it needs the radio info, and the
  enumeration **reloads `current_radios.cfg` over the whole radio array**,
  which destroyed the fake radio entry that the test had written. Fixed in
  `code/r_tests/test_video_rx_pipeline.cpp`: the enumeration is now done
  **before** the fake entry is written.
- the script then printed "The VRX half is verified against the live air",
  although the interface had never been opened. The test returned 1 both for
  "no video packet" and for "could not open". Fixed: the test returns **255**
  when the interface cannot be opened, and the script reports that as an
  error.
- the monitor interface was **not** deleted at the end: `ip link del
  rubyvrx0` fails with `EOPNOTSUPP` on the **virtual monitor of mac80211**
  (the driver has no monitor vif, so the monitor is a mac80211 interface
  that lives on the phy and is removed with `iw dev <name> del`). Fixed
  with the `delete_monitor()` helper of the script.

### 3. The second run: the production path on the live air

```bash
sudo -E ./code/r_tests/two_radio_rf_test.sh 60 149
```

- the monitor interface was created on the Eachine and confirmed at
  **5745 MHz** (channel 149);
- the **production** code opened it (`radio_open_interface_for_read()`,
  fd 3), libpcap installed the **production filter**, the FIFO write end
  was opened (fd 4) and the production loop read the interface for 60 s;
- the counter of the interface went from 2 to 6: **four real frames of the
  air** were delivered by the driver and copied by mac80211 to the monitor
  interface;
- the production filter **rejected all of them** — they are ordinary Wi-Fi
  traffic, not RubyFPV frames: "The radio packets received: 0, the video
  packets: 0, the bad packets: 0, the CRC errors: 0";
- the desktop player (started as the user of the session, never as root)
  received **nothing**, because there was no video on the air.

**This is not a proof of RubyFPV video reception.** No compatible RubyFPV
transmitter was on the air; the monitor interface captured arbitrary Wi-Fi
frames. What it proves is that the **capture half** of the radio path works
on the real hardware with the production code.

### 4. The defect of the production code that the run exposed

The log of the run showed the enumeration reading the phy of `wlan1` as
**66**:

```
Executing command raw: iw dev | grep -B 1 wlan1
Read process output: 37 bytes. Content: [  txpower 13.00 dBm
	Interface wlan1]
phy string: [txpower], length: 7
```

The `iw dev` output was parsed by taking the line **before** the name of
the interface, and that line is not the `phy#N` header when the radio has
more than one interface — which is exactly the case when a **monitor
interface** is on it, the configuration the RubyFPV receive path needs. The
index was built out of the `w` of "txpower". The phy 66 does not exist, the
channel list could not be read and **the Eachine was recorded with a band
mask of 0**, which makes a radio unusable (`hardware_radioindex_supports_
frequency()` returns 0 for every frequency).

**Fixed** in `code/base/hardware_radio.c`: the phy is now read from sysfs
(`/sys/class/net/<if>/phy80211/name`), with a correctly parsed `iw dev`
fallback, and a radio whose band list cannot be read is now reported
instead of being silently recorded with no band.
**Regression test added** to `test_radio_capabilities` (402 checks): the
phy of every radio is resolved again and it has to be the stored one and it
has to exist in `/sys/class/ieee80211`. The stored radio configuration
(`~/.config/rubyfpv/current_radios.cfg`, a detection cache that the
application regenerates at every start) was refreshed with the corrected
values: `wlan0` → phy#1, `wlan1` → phy#2, `wlan2` → phy#5.

### 5. The TX blocker, settled against the loaded driver and the running kernel

The loaded module was decompressed and disassembled:
`/usr/lib/modules/7.2.11-1-cachyos/kernel/drivers/net/wireless/realtek/rtw88/rtw88_core.ko.zst`.
In `rtw_ops_add_interface()` only four vif types are handled — **1** (AP),
**2** (STA), **3** (ADHOC), **7** (P2P client) — and everything else reaches
an `ud2`. `NL80211_IFTYPE_MONITOR` is **6**: **the rtw88 drivers cannot own
a monitor vif**, so mac80211 hands the injected frame over with a `NULL`
vif and the skb is dropped before it reaches the radio. Two runtime
observations confirm it: the monitor interface was created **without any
kernel oops**, and `ip link del` on it answers **EOPNOTSUPP** (it is the
virtual monitor of mac80211).

**The smallest change that would enable the TX half:** an adapter whose
in-tree driver owns a monitor vif — the **mt76** family (**MT7612U**,
ALFA AWUS036ACM, or the cheaper **MT7610U**), which sets
`IEEE80211_HW_WANT_MONITOR_VIF` and implements
`NL80211_IFTYPE_MONITOR` in `add_interface`. The out-of-tree
`aircrack-ng/rtl8812au` driver would also work but it needs DKMS, a kernel
module and a reboot, which the user rejected. The **roles are not swapped**:
the TP-Link stays the intended transmitter and the Eachine the intended
receiver.

### 6. The state of the suites (after the changes)

```
test_video_rx_pipeline     Tests: 158, Failures: 0
test_radio_capabilities    Tests: 402, failures: 0   (9 new checks)
test_menu_rf_profiles      Tests: 43, failures: 0
test_sik_serial_path       25 checks, 0 failures
test_config_persistence    20 checks, 0 failures
git diff --check           clean
```

### 7. The documents

`STATUS.md` §2.10 (the six separate findings of the RF test), §2.11 (the
defect of the production code), §3 (what is still not validated) and §4.1
(the TX blocker, with the driver-level evidence).
`QUESTIONS.md` Q8 (the approval: given and used for the VRX half) and Q9.

---

## 2026-10-22 — Session 5: the video retransmission loop, end to end, with the production code of both ends

The goal of this session was the last unproven piece of the **software** side
of the primary objective: the **video retransmission loop**. Until now the
station side of the loop was proven (test 14 of `test_video_rx_pipeline`) and
the vehicle side was only *read* from the sources; the **vehicle's own answer
code was never executed**. Now the whole loop runs with the production code
of the vehicle **and** of the station, and the video of the frame that had a
lost packet reaches the desktop player **byte for byte**.

The RF hop is still missing (it waits for the approval of Q8): the "air"
between the two ends is a capture file. Nothing here is claimed as a radio
result.

### 1. The new seam: the radio TX side to a capture file

`code/radio/radiolink.c` had a read seam (`radio_open_interface_for_read_from_file()`,
added in session 3) but no write counterpart: the production TX code could
only put frames on a live monitor interface, with `pcap_inject()`.

**Added: `radio_open_interface_for_write_to_file(interfaceIndex, captureFileName)`**
(`code/radio/radiolink.c`, declared in `code/radio/radiolink.h`).

- it creates a pcap handle with `pcap_open_dead(DLT_IEEE802_11_RADIO, 65535)`
  and opens the file with `pcap_dump_open()`;
- it sets the new flag `bTxToFile` of the TX runtime info;
- `radio_write_raw_ieee_packet()` was extended: when the interface is opened
  for a file, the frame is written with `pcap_dump()` — **the very bytes that
  would have gone on the air** — instead of `pcap_inject()`;
- `radio_close_interface_for_write()` closes the dump handle and the pcap
  handle of the file mode and clears the flag.

The frames that reach the file are built by the **production** TX code
(`radio_build_new_raw_ieee_packet()`: radiotap + 802.11 header + radio
packet + CRC), so the file has the same link type and the same bytes as a
monitor mode capture of a real link, and the production RX code reads it back
through the production filter and the production parsing.

The live interface is **not** touched in this mode: no monitor mode, no
channel, no promiscuous mode, no frequency is set. That matters, because
`wlan0` must never be touched and the test runs as a normal user.

### 2. The new vehicle-side test binary: `test_video_tx_resend`

`code/r_tests/test_video_tx_resend.cpp` (new) links the **real vehicle
modules** (`MODULE_VEHICLE` + `processor_tx_video.o`, `video_tx_buffers.o` is
inside `processor_tx_video.o`'s module, `process_radio_in_packets.o`,
`radio_links.o`, …) and has two modes:

```bash
./test_video_tx_resend --transmit <capture file>
./test_video_tx_resend --answer <request file> <capture file>
```

- `--transmit`: the vehicle builds the video packets with the real video TX
  code, they are framed with the real radio TX code and written to the
  capture file. **8 checks**: the video was built and sent, 30 video packets,
  all of them CRC-ok, none of them marked as a retransmission, none of them
  high priority, the first block has all its 4 data packets, the blocks are
  in order.
- `--answer`: the vehicle reads the request capture file with the real radio
  RX code, the real vehicle code (`process_data_tx_video_command()`) answers
  it and the answer is written to a second capture file. **7 checks**: the
  request was handled by the vehicle code, the answer contains the requested
  video packet, all the packets of the answer pass the production CRC check,
  all of them are marked as retransmissions, all of them are marked as high
  priority, the answer carries the retransmission id of the request and the
  retransmission id is not zero.

### 3. The new station-side mode: `--rtr-loop`

`code/r_tests/test_video_rx_pipeline.cpp` got a fourth mode:

```bash
./test_video_rx_pipeline --rtr-loop <vehicle capture> <request capture> <answer capture|-> <lost packet index>
```

It installs the fake test radio, opens the vehicle capture with
`radio_open_interface_for_read_from_file()` on the **DOWNLINK** port
(0x0F), reads the frames with `radio_process_wlan_data_in()` and
`packet_process_and_check()`, drops **one** video packet (the one whose
ordinal is the given index) and hands the rest to
`process_received_video_component_packet()`. Then it lets the production
`ProcessorRxVideo::periodicLoopProcessor()` do its work, takes the request
from the production high priority queue, checks it field by field (the
retransmission id, the stream index, the flags, the count, and the
`(blockId, packetIndex)` list, which must contain the lost packet), frames it
on the **UPLINK** port (0x0E) with the production TX code and writes it to the
request capture file.

With an answer capture file, it reads the answer with the production RX
code, feeds the packets to the production pipeline and verifies that the
answer completes the block, that the video that reaches the FIFO of the
desktop player is a **byte-for-byte copy** of the access units of the clip,
that the access unit that had the lost packet is byte-exact, and that the
retransmitted packet was counted and not discarded.

### 4. The orchestration script

`code/r_tests/video_retransmission_loop_test.sh` (new, executable) runs the
four steps in two processes and checks the exit status of each one:

```bash
cd /mnt/Giochi-e-Altro-2/RubyFPV/RubyFPV
RUBY_BUILD_ENV=desktop make -j20 test_video_rx_pipeline test_video_tx_resend
./code/r_tests/video_retransmission_loop_test.sh
```

**Result: all four steps green, 0 failures.** The logs are in
`/tmp/ruby/rtr_loop`.

```
--- 4. The station receives the answer and the video reaches the player ---
    PASS  the station requested the missing video packet               got=1
    PASS  the request asks for the lost packet                         got=yes
    PASS  the bytes on the air are the bytes of the request             got=yes
    PASS  the answer of the vehicle contains video packets              got=yes
    PASS  the video that reached the player is the video of the clip     got=yes
    The video that reached the player: 20960 bytes (the clip has 897117 bytes of access units).
    PASS  the access unit with the lost packet reached the player       got=yes
    PASS  the access unit with the lost packet is byte for byte correct got=yes
    PASS  the retransmitted packet was counted as retransmitted         got=yes
    PASS  the retransmitted packet was not discarded                    got=yes
```

The 20960 bytes are the first 8 access units of the clip: the loop delivers
the video of the frames that arrive, not the whole clip, and every delivered
byte is identical to the source.

### 5. Two production details the loop uncovered

- **A short video block always gets 1 EC packet, even at a 0 % EC rate.**
  `code/r_vehicle/video_tx_buffers.cpp` computes
  `uECPackets = ceil(dataPackets * fECRate + 0.001)` for the short-block
  adjustment, which is 1 when the rate is 0. A lost data packet of such a
  block is therefore reconstructed **locally**, with the EC packet, and no
  retransmission is requested. To exercise the retransmission the lost packet
  has to be one of a **full** block (4 data packets, no EC): block 1, packet
  1, the capture frame 5. The test drops that one.
- **The vehicle refuses a request whose source id is 0 or `MAX_U32`**
  (`code/r_vehicle/process_radio_in_packets.cpp`). The station's
  `g_uControllerId` is `MAX_U32` by default, so the mode sets it to
  `0x87654321`, the same id the vehicle side test uses.

### 6. The **sustained** loop, with the real desktop player

The loop of the section above was then repeated **many times in a row**, with
the **real desktop player** as the reader of the video FIFO, so the video of
every retransmitted frame is **decoded and displayed**, not only counted.

**The new mode** of the station test:
```bash
./test_video_rx_pipeline --rtr-loop-live <vehicle capture> <request capture> <answer capture> <vehicle binary> <iterations>
```

- the station runs the **vehicle test binary as a separate process** in every
  iteration (`fork()` + `execv()`), so the answer of every request is produced
  by the **production vehicle code** and not by a recorded file;
- the **lost packet changes** in every iteration: a different data packet of
  the full block is lost (the positions 1, 2 and 3 of the block, in turn). The
  packet to drop is found from the headers of the packets themselves (a block
  with 4 data packets and no EC packet), so the test does not depend on a
  hard coded index;
- the state is reset between the iterations with the **production** reset of
  the processor (`ProcessorRxVideo::fullResetState()`);
- the FIFO is **not** read by the test: the player is the reader, so the
  video that the production code writes is the one the player decodes;
- a `SIGALRM` **watchdog** ends the test with a clear error if the player
  does not read the video (the production write to the FIFO is a blocking one).

**One small production addition** was needed to make the test verifiable:
`rx_video_output_get_bytes_to_local_video_player()`
(`code/r_station/rx_video_output.h`) returns the number of the video bytes
that the **production** code wrote to the FIFO of the player. It is a
read-only accessor over a new counter (`s_uTotalBytesToLocalVideoStreamerPipe`).

**The script**: `code/r_tests/video_retransmission_live_test.sh` (new) warns
the user with a desktop notification, starts the real desktop player
(`ruby_player_desktop -p -d`), runs the sustained loop, captures what the
player shows (it raises the player window, takes the screenshot with
`spectacle` and restores the desktop) and stops the player.

**Result on this machine** (10 iterations):
```
The sustained retransmission loop: 10 iteration(s), 10 packet(s) lost, 10 request(s), 10 answer(s), 0 failure(s).
The video that reached the desktop player: 209600 bytes.
PASS  all the iterations of the sustained loop succeeded
PASS  every iteration lost a video packet
PASS  every iteration got an answer from the vehicle
PASS  the video reached the desktop player
```
and in the log of the real player:
```
[PlayerDesktop] Received the first video stream data (65536 bytes so far).
[PlayerDesktop] Detected the video stream format: video/x-raw(memory:CUDAMemory), format=(string)NV12, width=(int)1280, height=(int)720
[PlayerDesktop] Exit. Video stream: 209600 bytes read, 209600 bytes pushed, 80 frames decoded, codec detected: yes.
```
**80 frames decoded** = 10 iterations × 8 access units, and the screenshot of
the player window shows the "LOADING CONTENT" frame of the clip.

### 7. The state of the suites (after the changes)

```
make -j20 test_video_rx_pipeline test_video_tx_resend test_sik_serial_path \
     test_udp_to_radio test_radio_to_udp test_radio_capabilities \
     test_config_persistence test_menu_rf_profiles   (RUBY_BUILD_ENV=desktop)

./test_video_rx_pipeline                Tests: 158, Failures: 0
./test_radio_capabilities               Tests: 393, failures: 0
./test_menu_rf_profiles                 Tests: 43, failures: 0
./test_sik_serial_path                  25 checks, 0 failure(s)
./test_config_persistence               20 checks, 0 failures
./code/r_tests/video_retransmission_loop_test.sh   4 steps, 0 failures
./code/r_tests/video_retransmission_live_test.sh 10  10 iterations, 0 failures
git diff --check                        clean
```

`test_udp_to_radio` and `test_radio_to_udp` are **live radio tools**, not
regression suites: they open a real radio interface (`radio_open_interface_for_read()`
/ `radio_open_interface_for_write()`) and need the hardware and the
privileges of the two-radio test (Q8). They are built and they are not
touched by the changes of this session: the file mode of the radio layer is
a **separate** function, and the live path is unchanged.

The only remaining compiler warning in the touched files is the pre-existing
`code/radio/radiolink.c:1250: variable 'siDBGCountRx' set but not used`
(it is not in the diff of this session: `git diff code/radio/radiolink.c |
grep siDBGCountRx` returns nothing).

### 8. The documents

`STATUS.md` §1 (the new binaries and modes), §2.8 (the new verified result),
§2.9 (the sustained loop with the real player) and §3 (what is still not
validated) were updated; `QUESTIONS.md` was updated with the new state
(Q9, option 3 is done).

---

## 2026-10-22 — Session 4: the two-radio RF test, settled against the kernel sources — a real defect fixed, the script rewritten

The goal of this session was the last unproven step of the primary
objective: the **two-radio RF loopback** (the TP-Link Archer T2U Plus
transmitting, the Eachine WiFiLink receiving). The test is still waiting for
the approval of the user (`QUESTIONS.md` Q8), but the **software side** of it
was finished: the question "can these two adapters do it?" was settled with
the sources of the running kernel instead of with a guess, a real defect of
the production RX path was found and fixed, the test suite was extended to
the **exact** radiotap layouts of a Linux monitor interface and the script was
rewritten with the settled semantics.

### 1. What the kernel really does with these two radios (the settled facts)

The inherited claim — "the in-tree rtw88 drivers do not implement monitor
mode" — was **wrong**, and it was wrong in a way that mattered. What was
checked, and how:

| What | Where it was checked | Result |
|---|---|---|
| The monitor interface creation | the kernel sources of the running kernel (`net/mac80211/iface.c`) | `drv_add_interface()` is called for a `NL80211_IFTYPE_MONITOR` interface **only** with the active monitor or with `IEEE80211_HW_NO_VIRTUAL_MONITOR`; otherwise the **virtual monitor** is used and the driver is never asked to add a monitor vif |
| The flags of the rtw88 drivers | `/sys/kernel/btf/mac80211` (the enum of `ieee80211_hw.flags`) | `WANT_MONITOR_VIF` (12), `NO_VIRTUAL_MONITOR` (13) and `QUEUE_CONTROL` (19) are **not** set → the virtual monitor is used |
| The `MONITOR` case of the driver | the disassembly of the installed `rtw88_core.ko` | `rtw_ops_add_interface` handles iftype 1 (ADHOC), 2 (STA), 3 (AP), 7 (MESH) and **faults on the others** — but mac80211 never calls it with a monitor, so the `ud2` is never reached |
| The capture of the frames | `net/mac80211/rx.c` | `ieee80211_rx_monitor()` copies the frames that the driver delivers to the monitor netdevs, with the radiotap header that mac80211 builds |
| The link type of a monitor interface | libpcap 1.11 (`gencode.c`) | `DLT_IEEE802_11_RADIO` = **127**; the production gate of `code/radio/radiolink.c` accepts it, and libpcap applies the `ether[...]` offsets **after** the radiotap header, so the production filter matches the real frames |

**Conclusion:** the **capture** of the two rtw88 radios **works** (the
software monitor), and the **injection does not**: an injected frame goes
`ieee80211_monitor_start_xmit()` → `ieee80211_xmit()` → `__ieee80211_tx()`,
where, without the active monitor and without `WANT_MONITOR_VIF`, there is
no vif: mac80211 calls `ieee80211_tx_frags(local, NULL, …)` and the parallel
switch in `ieee80211_tx_dequeue()` ends with `info->control.vif = NULL` — the
frame is **dropped before it reaches the radio**. `pcap_inject()` still
reports a success, so the failure is only visible on the receiving radio.

**Consequence for the mission:** the VRX half of the RF milestone can be run
with the hardware that is on the machine; the TX half needs an adapter whose
driver supports the **monitor vif** — the **mt76** family (MT7612U /
MT7610U) — or the out-of-tree `rtl8812au` driver (which needs a new
approval: DKMS, a kernel module, a reboot).

### 2. A probe against the real libpcap

A small program (in the temporary directory of the job, not in the repo) wrote
real radiotap frames in a capture file with the link type 127 and applied the
**production filter** of `radiolink.c` to them:

```
A) the production filter, radiotap of 12 bytes:  1 frame read, 1 match
B) the production filter, radiotap of 26 bytes:  1 frame read, 1 match
C) the single terms of the filter, 26 bytes:     1 frame read, 1 match each
D) the single terms of the filter, 12 bytes:     1 frame read, 1 match each
E) the same filter with the uplink port byte:    0 frames read, 0 match
```

The filter matches the frames of a monitor interface whatever the length of
the radiotap header, and it discriminates the port.

### 3. A real defect of the production RX path, found and fixed

`code/radio/radiolink.c`: the antenna counter of the radiotap parsing was
**unbounded**, while the arrays that hold the antenna data have
`MAX_RADIO_ANTENNAS` (4) elements and live on the **stack**. A monitor
interface reports **one ANTENNA field per chain**, so a frame with more
chains than the arrays can hold made the code write **outside of them** —
a real memory-safety bug on the path that the RF milestone depends on. The
counter is now bounded.

### 4. Test 16: the four exact radiotap layouts of a Linux monitor interface

The radiotap header that mac80211 builds is **not** a single fixed layout: it
depends on the driver, on the rate and on the number of the chains. The
previous version of the test used one approximate 32-byte header. It now
builds the **four exact layouts** that mac80211 computes
(`ieee80211_rx_radiotap_hdrlen()` + `ieee80211_add_rx_radiotap_header()`):

| Layout | Length |
|---|---|
| the legacy rate, without the timestamp of the frame | 18 bytes |
| the legacy rate, with the timestamp of the frame | 26 bytes |
| the HT rate (the MCS field), without the timestamp | 21 bytes |
| the radiotap namespace (the card reports its chains) | 24 bytes |

Each of them is written in a capture file with the link type 127, read back
with the **production RX code** (`radio_open_interface_for_read_from_file()`,
`radio_process_wlan_data_in()`, `packet_process_and_check()`) and checked:
the frames come out **byte for byte identical** to the bytes that were on the
air, every packet passes the **production CRC check**, and the end of the
file is reached cleanly.

### 5. The script: `code/r_tests/two_radio_rf_test.sh`, rewritten

The script was written before the kernel semantics were settled and it was
wrong in three ways: it created the monitor interface **before** setting the
channel (so the monitor was born on the 2.4 GHz default channel of the radio,
with 5 GHz-only antennas attached), it claimed that the rtw88 drivers have no
monitor mode, and it started the TX half by default. It now:

- identifies the two radios by their **USB hardware id** and refuses `wlan0`
  and any interface that carries the default route (the test program refuses
  `wlan0` too);
- refuses any channel below 32 (2.4 GHz);
- brings the **managed** interface of the radio **down** (the kernel refuses
  to change the channel of a monitor interface while another interface of the
  same radio is up) and **moves the radio to the 5.8 GHz channel before the
  monitor interface is created**;
- **reads the frequency back** after the change and **aborts, deleting the
  monitor, if it is below 5000 MHz** — the Eachine must never stay on a
  2.4 GHz channel;
- restores the managed interfaces and deletes the monitors in the cleanup
  trap (also on an error or a Ctrl-C);
- runs the **VRX half against the live air** by default (the production
  filter is installed on a real monitor interface and the production parsing
  runs on it) and puts the **TX half behind `--tx`**, printing the reason why
  the injection cannot work with these adapters, so that the result is not
  mistaken for a fault of the production code;
- checks that the monitor interface really receives the frames of the air
  (the RX counters of the interface — `tcpdump` is not installed on this
  machine);
- starts the desktop player **as the user of the session** (after a
  `notify-send`), never as root, and reports precisely what each outcome
  means.

### 6. The state of the suites (after the changes)

```
./test_video_rx_pipeline     Tests: 158, Failures: 0   (the four radiotap layouts added 24 checks)
./test_radio_capabilities    Tests: 393, failures: 0
./test_menu_rf_profiles      Tests: 43,  failures: 0
./test_sik_serial_path       25 checks, 0 failures
./test_config_persistence    20 checks, 0 failures
make -j20 all RUBY_BUILD_ENV=desktop   0 errors
git diff --check             clean
```

### 7. The documents

`STATUS.md` §1 (the test counts), §2.4 (the four radiotap layouts and the
antenna defect), §3 (the two-radio test, the way it is run now), §4.1 (the
blocker, now settled with the evidence instead of with a guess) and §7 (the
adapter table and the exact requirement on the link type); `QUESTIONS.md` Q1
(the adapter that is still missing is a **monitor-vif** one) and Q8 (the
precise sequence of the script and the three possible outcomes);
`PROGRESS.md` (this entry).

---

## 2026-10-22 — Session 3: the video path, end to end — RF framing, sustained stream, retransmission, live video in the desktop player

The mission of this session was the primary objective of the port: **make the
end-to-end video streaming work**. The video path was traced in its 8 stages
first (done in the previous session), and this session closed the four stages
that were still open: the RF framing, the sustained stream, the
retransmission/forwarding and the display in the desktop application.

### 1. The radio path (the RF framing, both directions)
- **Test 11** (new): the video radio packets are wrapped into RF frames with
  the **production TX code** (`radio_build_new_raw_ieee_packet()`: the
  radiotap header + the 802.11 header + the radio packet, with the radio link
  packet index rewritten and the CRC recomputed), they are written to a
  capture file and they are read back with the **production RX code**
  (`radio_open_interface_for_read_from_file()`,
  `radio_process_wlan_data_in()`, `packet_process_and_check()`).
  *Result: 12/12 frames read, the bytes are **byte-identical** to the bytes
  that were on the air, 12/12 CRCs good, the end of the file is reached
  cleanly.*
- **Test 12** (new, sustained): 240 RF frames / 20 video frames through the
  same production path. *Result: 240/240 frames read, 240/240 CRCs good,
  280480 bytes delivered to the video FIFO in 1 ms, all 20 delivered frames
  byte-identical to the payload.*

### 2. The retransmission / forwarding path — established from the code, then verified byte by byte
- The semantics were **derived from the production code**, not invented:
  `ProcessorRxVideo::checkAndRequestMissingPackets()` builds a
  `PACKET_TYPE_VIDEO_REQ_MULTIPLE_PACKETS` request (retransmission id, video
  stream index, flags, count, (block index, packet index) pairs) and queues it
  in the high priority queue; the vehicle answers with the missing video
  packets marked with `PACKET_FLAGS_BIT_RETRANSMITED` +
  `PACKET_FLAGS_BIT_HIGH_PRIORITY` and the retransmission id in the video
  stream info (`code/r_vehicle/video_tx_buffers.cpp: _sendPacket()`).
- Two things had to be got right, and both were found by the failures:
  - The **frame packet index convention**: the low byte of `uFramePacketsInfo`
    is the index of the packet **inside the frame, counted forward** (the
    vehicle counts the packets of the frame from the end and decrements while
    it walks the frame backwards). With the wrong convention the station
    believes that the end of the frame is missing.
  - The **retransmission window**: with the retransmissions disabled (or with
    the window bits unset) the model computes a window of 0 ms and
    `checkAndDiscardBlocksTooOld()` throws the just-received block away before
    the request is built ("Discarded 2 blocks too old"). The test therefore
    enables `VIDEO_PROFILE_ENCODING_FLAG_ENABLE_RETRANSMISSIONS` with
    `DEFAULT_VIDEO_RETRANS_MS5_HP` (70 ms), the way the models do.
- **Test 13** (new): the request is built and every field is checked (36
  bytes, the type, the component, the high priority flag, the destination
  vehicle, a non-zero retransmission id, the stream index, the flags, the
  count, the block index and the packet index). The second request for the
  same missing packet carries the "re-requested" flag (0x01) and a **new**
  retransmission id.
- **Test 14** (new): the request is put on the air with the production TX
  builder — the bytes on the air are the bytes of the request, except the CRC
  (offsets 0-3) and the radio link index (offsets 14-15), and the CRC of the
  on-air packet is valid. The answer of the VTX is built and marked exactly
  as the vehicle side marks it; the station accepts it, completes the block and
  delivers the **whole 9576-byte frame** to the desktop player's FIFO, with
  the retransmitted packet counted as retransmitted and **not** discarded.
- Two test-only seams were needed because the real ones live in
  `ruby_rt_station.cpp`, next to its `main()`: `router_get_last_time_checked_for_video_packets()`
  and `router_is_eof()` (they are only used for the log messages of the
  requests). No production code was changed for them.
- **Test 15 — the forwarding to a second player (new).** The station can
  forward the received video over UDP to the local video player
  (`rx_video_output_video_data()` →
  `_rx_video_output_to_local_video_player_udp()`), the path that feeds
  another player with the same video. It is now covered: a UDP socket is
  bound on the port of the local video player (7012), a complete frame goes
  through the production receive path and the datagrams that arrive are
  **byte-identical to the H.264 payload**, one datagram per video packet.
  So the two output paths of the station are now covered by tests: the FIFO
  (the desktop player) and the UDP forward.

### 3. A real defect in the RX block buffer — found by the new tests
`code/r_station/video_rx_buffers.cpp` validated the video data length of
**every** packet of a block, including the EC packets. On an EC packet the
video data area holds the **FEC parity block**: the vehicle writes the parity
over the important header, so `uVideoDataLength` there is parity garbage and
not a length. The check read that garbage, rejected the valid EC packets and
the blocks were never completed (4 EC reconstruction tests failed).
*Fix*: the length check is applied to the data packets only, and the EC
packets have their own minimum-length check (the parity block must fit in the
packet). The EC reconstruction tests are green again.

### 4. Live video in the desktop application — demonstrated
- **New** `--live-feed` mode of `test_video_rx_pipeline`: it sends the real
  H.264 stream (`res/intro.h264`, 896970 bytes, 150 access units) through the
  **production** receive path in real time, at the frame rate of the clip
  (15 fps), exactly as the radio RX thread would:
  `process_received_video_component_packet()` → `ProcessorRxVideo` →
  `VideoRxPacketsBuffer` → `rx_video_output_video_data()` → the FIFO
  `/tmp/ruby/fifovideo_disp`. The packets are built the way the vehicle
  builds them (the same headers, the same CRC, the same block scheme, the
  forward frame packet index).
  The only difference from the other test cases is the FIFO: in the live feed
  the FIFO is read by the **desktop player**, not by the test (two readers of
  a FIFO split the stream between them), so the test does not open the read
  end. The FIFO is created only if it is not already there, so a player that
  has it open is not left reading from a deleted FIFO.
- *Verified on this machine* (the user was warned with a desktop notification
  before the screen was taken over, the window was restored afterwards):
  - the player logged "Received the first video stream data" and detected the
    format (`video/x-raw NV12 1280x720`);
  - the station detected the stream: "Detected video stream FPS: 15 FPS",
    "Detected H264 stream level: 41 (0x29)";
  - the player read **10.85 MB** from the FIFO in 120 s of streaming;
  - the decoded video was **rendered on screen**: the screenshots of the
    running player are the frames of the clip (the RubyFPV logo and the
    "LOADING CONTENT" animation) and they change while the stream runs.
- The demonstration is reproducible with two commands:
  ```bash
  ./ruby_player_desktop -p &          # the player reads the FIFO
  ./test_video_rx_pipeline --live-feed 120
  ```

### 5. The state of the suites
```
make -j20 all RUBY_BUILD_ENV=desktop     → 0 errors
test_video_rx_pipeline                    → Tests: 124, Failures: 0
test_radio_capabilities                   → Tests: 393, failures: 0
test_menu_rf_profiles                     → Tests: 43, failures: 0
test_sik_serial_path                      → 25 checks, 0 failure(s)
test_config_persistence                   → 20 checks, 0 failures
git diff --check                          → clean
```
(The video suite grew from 66 to 124 checks: the radio path, the sustained
radio path, the retransmission request, the retransmission loop and the
forwarding to a second player over UDP.)

**Still not validated:** the RF hop itself (no packet has ever been sent or
received over the air) and the telemetry path with a real flight controller.
The two-radio test is written and waits for the approval of the user
(`QUESTIONS.md` Q8).

### 6. The two-radio RF test — written, ready, waiting for the approval
The RF hop is the last unproven step, so the whole test was written in this
session, **but it was not run**: it needs privileges, and the user has to
approve them (`QUESTIONS.md` Q8).

- **Two new modes of `test_video_rx_pipeline`:**
  - `--rf-tx <seconds> <interface>` — the **VTX side**: the real video stream
    is sliced in real radio video packets, they are wrapped in the real RF
    frames by `radio_build_new_raw_ieee_packet()` (radiotap + 802.11, the
    downlink port, the CRC recomputed) and they are put on the air by
    `radio_write_raw_ieee_packet()` (a real `pcap_inject()`).
  - `--rf-rx <seconds> <interface>` — the **VRX side**: the frames are read by
    `radio_process_wlan_data_in()`, checked by `packet_process_and_check()` and
    handed over to `process_received_video_component_packet()`, so the video
    reaches the desktop player through the production pipeline.
  - Both refuse `wlan0` and `lo` in the program itself, not only in the
    script. The name of the interface is set in the in-memory radio entry of
    the process: no system configuration is read or written.
- **One-command script:** `code/r_tests/two_radio_rf_test.sh`. It finds the
  two radios by their **USB hardware id** (2357:0120 and 0bda:881a), refuses
  `wlan0` and any interface that carries the default route, creates the two
  monitor interfaces with `iw dev <radio> interface add ... type monitor`
  (never with `ip link add ... type monitor`, which would attach the monitor
  to an arbitrary phy), sets a **5.8 GHz** channel (the Eachine has 5 GHz-only
  antennas; the script refuses a 2.4 GHz channel), starts the desktop player
  **as the user of the session** (the app is never run as root), runs the two
  halves of the test, prints the result and deletes the monitor interfaces.
  It stops with the exact kernel error if the driver refuses the monitor mode.
- **The driver question, re-examined with new evidence.** The previous
  session concluded that the in-tree rtw88 driver cannot do monitor mode, from
  the absence of `monitor_start` / `monitor_stop` in `rtw88_core.ko`. That
  argument is **not decisive**: upstream rtw88 implements the monitor vif
  through `ieee80211_hw_set(hw, IEEE80211_HW_WANT_MONITOR_VIF)` and a
  `NL80211_IFTYPE_MONITOR` case in `rtw_ops_add_interface()`, which adds no
  new symbol; the in-tree support landed in kernel 6.13 (8812AU) and 6.14
  (8821AU) and the field reports describe a working monitor mode with
  injection. This machine runs kernel 7.2.11. `iw phy` lists `monitor` for
  the two rtw88 physes — but that listing comes from mac80211, which
  advertises `NL80211_IFTYPE_MONITOR` unconditionally, so it is not proof
  either. **The only way to settle it is the privileged test**, and that is
  exactly what the script does first. If it works, **no new hardware is
  needed** and Q1 is answered.
- **The adapter research for the case where it does not work** is in
  `STATUS.md` §7: the exact requirements (monitor mode, the radiotap link
  type, injection, a fixed 5.8 GHz channel, 5.8 GHz antennas, the USB id
  entry in `code/base/hardware_radio.c`) and the candidates with the
  evidence — the recommendation is an **MT7612U** (ALFA AWUS036ACM) or an
  **MT7610U** (ALFA AWUS036ACHM) adapter: 5 GHz, in-tree `mt76` driver,
  monitor mode and injection, no DKMS, no reboot. The AR9271 and the RT3572
  are 2.4 GHz only and the mt7921u is reported broken.

---

## 2026-10-22 — Session 2: the decisions of the user, persistent configuration, capability gate

The user answered the 7 questions of `QUESTIONS.md`. The decisions are
recorded there; this is what was implemented because of them.

### Q7 — the stale fixture process (done)
- PID 129064 was verified before it was touched: cmdline
  `python3 /tmp/ruby_rf_fixtures_quarantine/sik_emu.py`, executable
  `/usr/bin/python3.14`, started 2026-10-21 16:17:38, and the `emu.pid` file
  of the fixture contained the same PID.
- It was terminated with SIGTERM (graceful). The unrelated `python3 -q`
  process (PID 912) and the fixture files were left untouched.

### Q6 — the radio configuration is skipped when the capability is missing
- **New** `hw_process_has_net_admin_capability()` in
  `code/base/hardware_procs.h/.cpp`: it reads `CapEff` from
  `/proc/self/status` and checks bit 12 (`CAP_NET_ADMIN`). No library, no
  privilege change, no `setcap`.
- `code/r_start/r_initradio.cpp`: a supported wifi radio is configured only if
  the capability is present. Otherwise the log says
  "Configuring wifi radio interface 1 (wlan1): skipped, this process does not
  have the CAP_NET_ADMIN capability needed to configure a wireless interface.
  The radio interface is left unconfigured.", the
  `FILE_TEMP_RADIOS_CONFIGURED` marker is **not** written (so the radio is
  configured at a later start, if the process has the rights by then) and the
  radios are reported as not configured.
- `code/r_central/osd/osd_stats_radio.cpp`: a radio that was never configured
  shows "Not Configured" instead of "Set Freq Failed".
- *Verified: a full start now logs **0 soft errors**.*

### Q3 — the desktop configuration is now persistent
- **New** `code/base/config_paths.h` / `code/base/config_paths.c`:
  - `config_get_config_folder()`, `config_get_config_models_folder()`,
    `config_get_vehicle_history_folder(id)`,
    `config_get_osd_plugins_folder()`, `config_get_core_plugins_folder()`;
  - on the desktop the folders are under `$HOME/.config/rubyfpv/`
    (`$HOME` from the environment, with the passwd entry as a fallback);
    the embedded platforms keep their fixed paths;
  - the folders are created if they do not exist, with the default
    permissions of the system (the `chmod 777` of the configuration files is
    now skipped on the desktop, it is kept on the embedded boards);
  - `config_copy_settings_folder()` copies the files of a folder to another
    one, **never overwriting** a file that is already there, writing through a
    temporary file that is renamed at the end;
  - `config_migrate_legacy_desktop_config()` copies the settings of the old
    desktop installation (`/tmp/ruby/config/`, `/tmp/ruby/plugins/`) to the
    new location, without deleting the old files.
- `code/base/config_file_names.h`: the `FOLDER_CONFIG`,
  `FOLDER_CONFIG_MODELS`, `FOLDER_OSD_PLUGINS` and `FOLDER_CORE_PLUGINS`
  macros are now the runtime accessors; the per-platform literals became
  `FOLDER_*_PATH`. The `FOLDER_VEHICLE_HISTORY` format macro was replaced by
  `config_get_vehicle_history_folder()` (the only user is
  `code/base/models.cpp`).
- `code/r_start/ruby_start.cpp`: the migration runs at the very beginning of
  `main()`, **before** anything reads the configuration — otherwise the
  application sees an empty configuration folder and thinks that it is
  installed for the first time (it rebooted itself and reset the boot
  counter). This was found in the first test run and fixed.
- `Makefile`: `config_paths.o` added to `MODULE_BASE`, `MODULE_MINIMUM_BASE`
  and to `ruby_player_desktop`; new target `test_config_persistence`.
- **New regression test** `code/r_tests/test_config_persistence.cpp`
  (20 checks): the configuration folder is the one of the user and not a
  temporary one, the sub-folders are created, a setting written with the real
  production code (`radio_profiles_save()`) is read back **by a new process**
  (the test re-executes itself — a real restart), the migration copies the
  old settings without overwriting the existing ones and without deleting the
  old files. The test uses a home folder of its own.
  *Result: 20 checks, 0 failures.*
- *Verified on this machine*: the 17 files of the old installation were
  migrated to `~/.config/rubyfpv/`, the old files are still in
  `/tmp/ruby/config/`, the boot count continued over two restarts (23 → 24 →
  25, it was reset to 1 before the fix), `ui_preferences.cfg` and
  `controller_settings.cfg` are byte-identical before and after a stop/start
  cycle, and the start log has 0 soft errors.

---

## 2026-10-22 — Session: SiK radio path, lifecycle, configuration persistence, honest logging

### Done and verified

**Radio — SiK serial path (mission priority #2)**
- `code/base/hardware_radio_sik.c`
  - Detection now walks a **dynamic candidate list** instead of the fixed
    `/dev/ttyUSB0..2` list: the `RUBY_SIK_PORTS` override (colon separated),
    the enumerated serial ports that are not used for telemetry / data-link /
    core-plugin, `/dev/ttyACM*` (CDC-ACM radios), and `/dev/serial/by-id/*`.
    The embedded platforms keep the old fixed list.
  - `hardware_radio_sik_open_for_read_write()` no longer requires the device
    to be in the enumerated serial-ports table: it falls back to the
    airspeed-derived baud rate and registers the port on the fly.
- `code/base/hardware_serial.h/.c` — new `hardware_serial_add_port()` API, the
  serial-port table raised to 16 ports, CH340/ACM enumeration enabled on the
  desktop, and a runtime-detected SiK radio is written to `hw_serial.cfg` so
  it is known at the next start.
- **New regression test** `code/r_tests/test_sik_serial_path.cpp` (25 checks)
  + `Makefile` target `test_sik_serial_path`: a SiK emulator on a PTY drives
  the real detect → open → write → RX-thread path; the packet must arrive
  byte-identical. The test saves/restores the two config files it touches and
  never probes a real serial device.
  *Result: 25 checks, 0 failures.*

**Application lifecycle (mission priority #1)**
- `hw_stop_process_graceful()` (new, `code/base/hardware_procs.h/.cpp`):
  SIGTERM → wait → SIGKILL. Used by the controller and by `ruby_start -stop`.
- `code/r_station/ruby_controller.cpp` — the controller now stops the user
  interface on exit (no orphan window) and gives it 5 s to close gracefully.
  *Verified: "Caught signal to stop: 15" → "Finished shutdown UI" in ~1 s, no
  SIGKILL.*
- `code/r_start/ruby_start.cpp` — new **`./ruby_start -stop`** command that
  stops every Ruby process.
  *Verified: after it, no `ruby_*` process remains.*
- `code/r_central/launchers_controller.cpp` — the shutdown wait was waiting
  for a process name that is never started ("ruby_rt_station" instead of the
  router's own name); fixed.

**Configuration persistence (mission priority #5) — the biggest find of the session**
- The desktop build deleted its **entire configuration folder on every start**:
  the startup cleanup of the stale named pipes used `rm -rf /tmp/ruby/*`, and
  on the desktop the configuration lives in `/tmp/ruby/config/`.
  *Found with an `LD_PRELOAD` shim that logged every `system`/`popen`/`unlink`
  call; the culprit was `POPEN rm -rf /tmp/ruby/*`.*
- Fix: the cleanup now removes **only the named pipes, each one by explicit
  name** (`RUBY_FIFO_NAMES` in `code/base/config_obj_names.h`, which is now
  the single definition of all the pipe names; `code/base/ruby_ipc.c` uses it
  too).
  *Verified: a stop/start cycle leaves the config folder untouched and the
  settings are loaded from it ("Read previous system type: 0").*
- The first-run "missing file" messages now create the file with the default
  value instead of repeating on every start (`code/base/config.c`
  `load_simple_config_fileI/U`, `code/base/plugins_settings.cpp`).
- The plugin folders are now created on every platform (`ruby_start.cpp`): the
  OSD/core plugin search failed on the desktop because the folders were only
  created on the embedded platforms.

**Honest error reporting (mission priority #1)**
- The TX power message claimed the power "changed to: 10" even when the
  command had failed with `EPERM`. It now reports the failure
  (`code/base/hardware_radio_txpower.c`, 3 card families).
- "Found unsupported radio (wlan0) … skipping" and the matching
  "Radio interface 2 is not a wifi radio type" are now informational messages,
  not soft errors: an adapter RubyFPV does not know how to drive is normal on a
  desktop and `wlan0` must not be configured (`code/base/hardware_radio.c`,
  `code/r_start/r_initradio.cpp`).
- The absent `ruby_rt_station` process is no longer an error when there is no
  radio to talk to. The launcher and the start script now share one rule,
  `hardware_radio_has_serial_radios()` (new, `code/base/hardware_radio.h/.c`).
  *Verified: the "ruby_rt_station is not running" spam (10 errors per start)
  is gone.*
- The device-tree serial number is only read on the embedded platforms
  (`code/base/models.cpp`, `code/utils/utils_controller.cpp`): on a desktop
  there is no device tree, and the id is still generated.
- The base Ruby version file is now found in the application folder:
  "Read raw base Ruby version: [11.8] from file (version_ruby_base.txt)".
  Before the fix the version was unknown and an error was logged on every start
  (`code/base/config.c`).

**Desktop robustness (mission priority #1)**
- `hw_get_nice_value_for_raw_priority()` (new): a normal user can only lower
  the priority, so the negative nice values are clamped. The "nice: cannot set
  niceness: Permission denied" spam is gone.
- `media_scan_files()` uses the absolute `FOLDER_MEDIA` (the relative `media/`
  depended on the CWD of the process).
- `hw_process_get_pids()` uses `pgrep -f "[/]name"` for names longer than 15
  characters.

**Safety, checked at every step**
- `wlan0` stayed UP on "Issam 2.4g" (2437 MHz) and `ping 1.1.1.1` → 0% loss,
  before and after every run.
- `wlan1` stayed `type managed`, with no channel and no frequency set.
- The wireless regulatory domain is not changed by the desktop build.
- The 5 GHz-only guard for card model 7 was not touched.
- The application was never run as root, no `sudo` was used, no package was
  installed, no system-wide permission or capability was changed.

**Final state of the session**
```
make -j20 all RUBY_BUILD_ENV=desktop     → 0 errors
test_video_rx_pipeline                    → Tests: 66, Failures: 0
test_radio_capabilities                   → Tests: 393, failures: 0
test_menu_rf_profiles                     → Tests: 43, failures: 0
test_sik_serial_path                      → 25 checks, 0 failure(s)
git diff --check                          → clean
```
Cumulative port diff: 55 source files, +2449 / −326 lines.

**Still not validated (unchanged):** live RF reception, RF transmission, the
telemetry path with a real flight controller. See `STATUS.md` §3 and §4, and
the open questions in `QUESTIONS.md`.

---

## 2026-10-21 — Previous milestone: video reception

- Fixed a real bug in `code/r_station/rx_video_recording.cpp`: the last
  ≤ 64000 bytes of every recording were lost (the buffer was only written to
  the pipe when it filled). Added `_recording_move_pipe_data_to_file()` and a
  flush of the pending buffer when the recording thread exits.
- New test `code/r_tests/test_video_rx_pipeline.cpp` (66 checks, headless):
  drives the real receive path (packet → `ProcessorRxVideo::handleReceivedVideoPacket()`
  → `VideoRxPacketsBuffer` → reassembly/FEC → FIFO + recording). Complete
  frames, ordering, incomplete/malformed blocks, bad CRC, unknown vehicle, EC
  reconstruction, and a recorded `.h264` accepted by `ffprobe`/`ffmpeg`.
- The desktop player was verified with a real H.264 stream over the station
  FIFO (format detected, frames decoded, rendered on screen).
- The Wi-Fi cards were characterised in **detection only** — no mode, no
  channel, no frequency was changed on either interface.

---

## Still open (see `QUESTIONS.md`)

1. A **monitor-vif capable** radio adapter (or an out-of-tree driver) — the
   two USB radios of this machine **can receive** (the mac80211 software
   monitor) but **cannot transmit**: mac80211 drops the injected frames
   because the rtw88 drivers announce neither a monitor vif nor the queue
   control. The candidate is an **MT7612U** (mt76, in-tree, injection
   verified). See `STATUS.md` §4.1 and §7. — **the decision changed**: the
   out-of-tree driver for the **existing** TP-Link is now prepared
   (`code/radio/driver_88xxau/`, `STATUS.md` §2.12) and waits for the
   approval of `QUESTIONS.md` Q10. The MT7612U stays the simplest option
   (in-tree, no DKMS, no reboot).
2. A 2.4+5.8 GHz adapter for the 2.4 GHz link (the present card has 5 GHz-only
   antennas).
3. Where the desktop configuration should live (`/tmp/ruby/config` is
   volatile). — **decided and done**: the configuration is in
   `~/.config/rubyfpv/`.
4. Whether the application should be started automatically (`systemd --user`).
5. Whether the vehicle side is in scope for the desktop port.
6. Whether the radio configuration should be skipped when the privileges for
   it are missing. — **decided and done**.
7. The stray SiK emulator fixture from an earlier session (PID 129064). —
   **stopped and verified**.
