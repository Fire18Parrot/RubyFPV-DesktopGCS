# The out-of-tree driver for the TP-Link Archer T2U Plus (RTL8821AU)

This directory holds the preparation, the installation and the reversible
switch to the out-of-tree driver that RubyFPV needs on the TX side.

It answers one question: **can the TP-Link Archer T2U Plus (USB
`2357:0120`, RTL8821AU) that is already on this machine transmit the RubyFPV
video frames, with the same adapter and without changing the network
configuration of the machine?**

The answer found here is *yes, with an out-of-tree driver*, and this
directory contains everything that is needed to install it, to use it and to
undo it.

---

## 1. The problem that this solves

The RX side works: the Eachine WiFiLink (`0bda:881a`, the in-tree
`rtw88_8812au` driver) captures the frames in monitor mode and the
production RX code parses them.

The TX side does not work, with **any** of the two adapters of this
machine, and the reason is the driver, not the RubyFPV code:

* the in-tree `rtw88` drivers (both `rtw88_8812au` and `rtw88_8821au`) do
  **not** announce a monitor vif (`IEEE80211_HW_WANT_MONITOR_VIF`), nor the
  queue control (`IEEE80211_HW_QUEUE_CONTROL`), nor the active monitor;
* mac80211 therefore hands the injected frame to the driver **with no vif at
  all**, and the frame never reaches the air;
* `pcap_inject()` still reports a success, so the failure is only visible
  on the receiving radio.

This is why the two-radio RF test (the RX half only) captured 0 frames with
0 video packets: there was nothing on the air, because the TX half cannot
put anything there.

---

## 2. The driver

| Item | Value |
| --- | --- |
| Repository | `https://github.com/aircrack-ng/rtl8812au.git` |
| Branch | `v5.6.4.2` |
| Pinned commit | `734485506a30d6237c2deaad666a19f8ca5379f2` ("Merge pull request #1254 from paralin/fix-6.19") |
| Module file | `88XXau.ko` |
| USB driver name | `rtl8812au` (this is the name that the kernel reports in `DRIVER=`) |
| Licence | GPL, the same licence as the in-tree driver |

The same driver is already known to the RubyFPV code: the project loads
`88XXau` with `sudo modprobe 88XXau ...` in
`code/base/hardware_radio.c` (`hardware_load_driver_rtl8812au()`), and it
recognises the `rtl8812au` driver name in the uevent of the interface
(`code/base/hardware_radio.c`, the `strstr(pszDriver,"rtl8812au")` test)
and maps it to `RADIO_HW_DRIVER_REALTEK_RTL8812AU`.

This is a second, independent reason to prefer this driver: **with the
in-tree `rtw88_8821au` the TP-Link card is not even recognised by the
project** (the string `rtw88_8821au` matches none of the patterns of the
enumeration), while with the out-of-tree driver it becomes a supported
Realtek radio.

### 2.1 The adapter is supported

`os_dep/linux/usb_intf.c:208`:

```c
{USB_DEVICE(0x2357, 0x0120), .driver_info = RTL8821}, /* TP Link - T2U Plus */
```

The exact USB hardware ID of the TP-Link Archer T2U Plus is in the device
table of the driver, with the RTL8821 chip configuration.

### 2.2 Monitor mode is supported

`Makefile:59`: `CONFIG_WIFI_MONITOR = y`

The monitor interface is created with the radiotap link type, exactly like
the interface that the production code expects
(`os_dep/linux/ioctl_cfg80211.c:4936`):

```c
mon_ndev->type = ARPHRD_IEEE80211_RADIOTAP;
```

`ARPHRD_IEEE80211_RADIOTAP` is the link type that the production code reads
through libpcap (`DLT_IEEE802_11_RADIO`, 127) and that it filters on.

### 2.3 The injection behaviour that RubyFPV needs is supported

This is the point that matters. The production TX code builds a radiotap
header followed by a raw 802.11 DATA frame and writes it to the monitor
interface with `pcap_inject()`
(`code/radio/radiolink.c`, `radio_write_raw_ieee_packet()`; the radiotap
headers are at `code/radio/radiolink.c:105` and `:124`).

In the driver, that write arrives at `rtw_xmit_entry()`
(`os_dep/linux/xmit_linux.c:547`), which routes to
`rtw_monitor_xmit_entry()` (`core/rtw_xmit.c:4401`) when the firmware state
is `WIFI_MONITOR_STATE`. That function:

1. parses the radiotap header and checks its version and length;
2. copies the 802.11 frame out of the skb;
3. injects it, honouring the `RATE` and `MCS` fields of the radiotap header.

The driver also has its own monitor netdev path
(`rtw_cfg80211_monitor_if_xmit_entry()`,
`os_dep/linux/ioctl_cfg80211.c:4721`), used by the monitor interface that
`iw dev <if> set type monitor` creates — this is the command that the
production code runs (`code/base/hardware_radio.c:2007` and `:2075`).

So the exact sequence of the production code — `iw dev <if> set type
monitor`, then `pcap_inject()` of a radiotap + 802.11 frame — is supported
by this driver, and it is *not* supported by the in-tree `rtw88` drivers.

### 2.4 The module parameters

`os_dep/linux/os_intfs.c:80`:

| Parameter | Default | Meaning |
| --- | --- | --- |
| `rtw_monitor_disable_1m` | 0 | Disable the default 1 Mbps rate for the injected frames |
| `rtw_monitor_retransmit` | 0 | Retransmit the injected frames |
| `rtw_monitor_overwrite_seqnum` | 0 | Overwrite the sequence number of the injected frames |

**`rtw_monitor_disable_1m=1` is mandatory for RubyFPV.** With the default
value the driver forces every injected frame to 1 Mbps, and 1 Mbps does not
exist on the 5 GHz channels: the frame would be accepted by the driver and
would never reach the air. The scripts of this directory always load the
module with `rtw_monitor_disable_1m=1`.

Note also that the parameter `rtw_tx_pwr_idx_override` that the RubyFPV code
uses when it loads `88XXau` (`code/base/hardware_radio.c:1403`) does not
exist in this version of the driver. The kernel ignores the unknown
parameters of a module with a warning in the kernel log, so the load still
succeeds, but the parameter has no effect. The out-of-tree driver has its
own power management parameters (`rtw_tx_pwr_by_rate`,
`rtw_tx_pwr_lmt_enable`, `rtw_power_mgnt`, `rtw_country_code`).

---

## 3. Compatibility with the kernel of this machine

The kernel is `7.2.11-1-cachyos`. The released driver (`5.6.4.2`) **does
not compile as it is** on that kernel. The breaks were all identified
against the local headers (`/usr/lib/modules/7.2.11-1-cachyos/build/include`)
and they are all fixed by `88xxau-linux-7.2.patch`, in this directory:

| Break | Fix |
| --- | --- |
| `struct cfg80211_ops`: `add_key`, `get_key`, `del_key`, `set_default_mgmt_key`, `add_station`, `del_station`, `change_station`, `get_station`, `dump_station` now take a `struct wireless_dev *wdev` instead of a `struct net_device *ndev` | the 9 callbacks take the `wireless_dev` and derive the `netdev` from it, under `#if (LINUX_VERSION_CODE >= KERNEL_VERSION(7, 2, 0))` |
| `cfg80211_new_sta()` and `cfg80211_del_sta()` take a `struct wireless_dev *` | the 3 call sites pass `padapter->rtw_wdev` |
| `struct cfg80211_ops::remain_on_channel` gained a `const u8 *rx_addr` | the callback takes it under the same version guard |
| `set_wiphy_params`, `set_tx_power` and `get_tx_power` gained an `int radio_idx` (kernel 6.17 and later) | the callbacks take it under `#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 17, 0))` |
| `strncpy()` was removed from the kernel headers | `rtw_compat_strncpy()`, a static inline with the same semantics, in `include/osdep_service_linux.h` |
| `from_timer()` was removed | `timer_container_of()` (kernel 6.16 and later) |
| `del_timer_sync()` was renamed `timer_delete_sync()` (kernel 6.15 and later) | `timer_delete_sync()` under the version guard |
| `struct pppoe_hdr::tag[]` and `struct pppoe_tag::tag_data[]` are hidden from the kernel code | `PPPOE_HDR_TAG()` and `PPPOE_TAG_DATA()`, that compute the pointers |

The patch is written for the 7.x kernel and it keeps the old code paths
intact for the older kernels, so the tree still builds on the kernels that
the driver supported before.

Two upstream sources were used as a reference for the direction of the port:
the aircrack-ng pull requests **#1264** ("TOFM:fix-kernel-7.0-build", which
adds the `radio_idx` parameters and the timer changes) and **#1267** (which
adds a `compat_fixes.h` with the `from_timer` and `del_timer_sync`
fallbacks). The patch of this directory is not a copy of them: it was
written against the local 7.2.11 headers, because the copy of the Buildroot
patch for the Linux 7.2 that circulates on the mailing list is mangled by
the RFC-3676 line wrapping.

**The patch has not been compiled yet.** The compilation is the first step
of the plan below, and it is the step that needs the approval.

---

## 4. Secure Boot and the module signing

| Item | Value | Consequence |
| --- | --- | --- |
| `SecureBoot` efivar | `0` (disabled) | the kernel does not enforce the signature of the modules |
| `SetupMode` efivar | `1` | the firmware is in setup mode |
| `/sys/kernel/security/lockdown` | `[none]` | no lockdown restriction |
| `dkms` | 3.4.4 | available |
| `gcc` | 16.2.1 | available |
| kernel headers + `Module.symvers` | present for `7.2.11-1-cachyos` | the module can be built |

**No MOK has to be enrolled and no reboot is needed.** The module that DKMS
builds is not signed, and this kernel accepts the unsigned modules because
Secure Boot is off and the lockdown is `none`. The DKMS package is declared
with `AUTOINSTALL="yes"`, so the module is rebuilt automatically when the
kernel is updated.

If Secure Boot were ever enabled, the module would have to be signed and
enrolled in the MOK list, with a reboot. That is not the case here.

---

## 5. The plan, the commands and their expected effects

Every command below is **presented for approval and not executed yet**.

### Step 1 — prepare the source (no root, nothing changes on the system)

```bash
cd /mnt/Giochi-e-Altro-2/RubyFPV/RubyFPV/code/radio/driver_88xxau
./prepare_source.sh
```

*Effect*: clones `https://github.com/aircrack-ng/rtl8812au.git` into
`/tmp/ruby/driver_88xxau/rtl8812au`, checks out the pinned commit
`7344855` and applies `88xxau-linux-7.2.patch`. Nothing is installed, no
privilege is needed, the system is not touched.

### Step 2 — compile the driver (no root, nothing is installed)

```bash
./build_module.sh
```

*Effect*: runs `make -j$(nproc) KSRC=/lib/modules/7.2.11-1-cachyos/build`
in the prepared tree and produces `88XXau.ko`. If the port is complete the
build ends with the module file; if something is still missing, the script
prints the list of the remaining compile errors (the full log is in
`/tmp/ruby/driver_88xxau/build.log`). **The module is not installed and the
running driver is not touched.**

### Step 3 — install the module with DKMS (root, changes the system)

```bash
sudo ./install_module.sh
```

*Effect*:

* copies the patched source tree to
  `/usr/src/rubyfpv-88xxau-5.6.4.2-rubyfpv1`;
* registers it with DKMS as `rubyfpv-88xxau/5.6.4.2-rubyfpv1`;
* builds it for `7.2.11-1-cachyos` and installs the module file in
  `/lib/modules/7.2.11-1-cachyos/updates/88XXau.ko`.

The running driver is **not** changed by this step: the module is installed
but not loaded. The in-tree driver keeps working.

### Step 4 — switch the adapter to the out-of-tree driver (root)

```bash
sudo ./use_out_of_tree_driver.sh
```

*Effect*:

* identifies the TP-Link adapter by its USB hardware ID `2357:0120` and
  refuses to continue if it is the interface that carries the default route
  of the machine (that is `wlan0`, the Intel AX210, which is never touched);
* brings the adapter interface down and removes the extra interfaces (the
  monitor ones) of the same adapter;
* unloads the in-tree module `rtw88_8821au`;
* loads `88XXau` with `rtw_monitor_disable_1m=1`;
* waits for the adapter to be enumerated again and prints its new interface
  name and its new driver.

The interface name of the adapter can change across the reload (this is
exactly why the project identifies the radios by their USB hardware ID).
The adapter is briefly absent from the system; `wlan0` and `wlan1` are not
touched at all.

If the in-tree module refuses to be unloaded (it is bound to the adapter),
the script says so and asks for the adapter to be unplugged and plugged
back; it does not force anything.

### Step 5 — go back to the in-tree driver (root)

```bash
sudo ./restore_in_tree_driver.sh
```

*Effect*: unloads `88XXau`, loads `rtw88_8821au` again, removes the
optional blacklist file and prints the state of the adapter. The out-of-tree
module stays installed but it is not loaded.

### Step 6 — remove the out-of-tree driver from the system (root)

```bash
sudo ./remove_module.sh
```

*Effect*: `dkms remove rubyfpv-88xxau/5.6.4.2-rubyfpv1 --all`, the removal
of `/usr/src/rubyfpv-88xxau-5.6.4.2-rubyfpv1`, the removal of the optional
blacklist and a `depmod -a`. The system is then exactly as it was before
this work started.

### The optional persistent switch

`sudo ./use_out_of_tree_driver.sh --persist` also writes
`/etc/modprobe.d/rubyfpv-88xxau-blacklist.conf`, that blacklists
`rtw88_8821au`, so that the adapter always binds to the out-of-tree driver,
even after a reboot or after the adapter is plugged back in. Without it,
the adapter goes back to the in-tree driver at the next boot. This is a
system-wide configuration change, so it is optional and it is not done by
default.

---

## 6. What is proven and what is not

**Proven, by reading the code and by checking the system:**

* the driver supports the exact USB hardware ID of the adapter;
* the driver supports the monitor interface and the radiotap link type that
  the production code uses;
* the driver has the injection path that the production code needs, and the
  in-tree drivers do not;
* the module parameters that the 5.8 GHz band needs are known;
* the port covers every API break of the 7.2.11 kernel that the driver
  meets;
* the system accepts an unsigned out-of-tree module (Secure Boot off,
  lockdown none), so no MOK and no reboot are needed;
* the switch and the restore are both scripted, and both refuse to touch
  the interface that carries the network connection.

**Not proven yet:**

* **the driver has never been compiled on this kernel** — step 2 is the
  proof, and it is the step that needs the approval;
* the module has never been loaded on this machine;
* **no RF frame has been transmitted with the out-of-tree driver**: the
  injection of the production code on the real air is still to be done, and
  it has to be done with the approval of the RF test, and only after the
  driver is proven to work.

Nothing in this directory, and nothing that was done to prepare it, claims
that the RF link works. The claim that is made here is narrower: the
adapter that is already on this machine can be given a driver that
supports the injection that RubyFPV needs, in a reversible way, without
touching the network connection of the machine.

---

## 7. The files of this directory

| File | What it does | Root? |
| --- | --- | --- |
| `README.md` | this document | - |
| `common.sh` | the shared definitions and the safety helpers | - |
| `88xxau-linux-7.2.patch` | the port of the driver to the Linux 7.x kernel API | - |
| `prepare_source.sh` | clone at the pinned commit and apply the patch | no |
| `build_module.sh` | compile the module, install nothing | no |
| `install_module.sh` | DKMS add/build/install | yes |
| `use_out_of_tree_driver.sh` | switch the adapter to the out-of-tree driver | yes |
| `restore_in_tree_driver.sh` | bring the in-tree driver back | yes |
| `remove_module.sh` | remove the out-of-tree driver from the system | yes |
