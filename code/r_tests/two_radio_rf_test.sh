#!/usr/bin/env bash
#
# The two-radio RF loopback test.
#
# It runs the real video path over the air, between the two USB radios of the
# machine:
#
#   TX: the TP-Link Archer T2U Plus (USB 2357:0120) sends the real video
#       stream, in real RF frames, with the production TX code.
#   RX: the Eachine WiFiLink (USB 0bda:881a) receives them through the
#       production RX code and the production video pipeline, and the video
#       reaches the desktop player (ruby_player_desktop -p).
#
# The radios are identified by their USB hardware ID, not by their interface
# name. The interface that carries the network connection of the machine
# (wlan0, the Intel AX210) is never used and never reconfigured: the script
# refuses any interface that is not a USB radio and it refuses wlan0 by name.
#
#
# What the kernel does with these two radios (checked on this machine, kernel
# 7.2.11, with the in-tree rtw88_8812au / rtw88_8821au drivers):
#
#   * CAPTURE works. mac80211 provides the monitor interface by itself (the
#     "software monitor"): the monitor interface is created on the radio and
#     it receives the frames that the driver delivers, with the radiotap
#     header that mac80211 builds. The production RX code reads exactly that
#     format: the link type of a Linux monitor interface is
#     DLT_IEEE802_11_RADIO (127 in libpcap) and the production code accepts
#     it and filters it correctly.
#
#   * INJECTION does not work with these two drivers. The driver does not
#     announce a monitor vif (IEEE80211_HW_WANT_MONITOR_VIF), nor the queue
#     control (IEEE80211_HW_QUEUE_CONTROL), nor the active monitor, so
#     mac80211 hands the injected frame over to the driver with no vif at
#     all and the frame never reaches the air. pcap_inject() still reports a
#     success, so the failure is only visible on the receiving radio.
#     The TX half of this test is therefore expected to receive nothing with
#     these two adapters: the TX half needs an adapter whose driver
#     supports the monitor vif (the mt76 family, MT7612U / MT7610U, does).
#
# The script therefore does two things:
#
#   * by default it verifies the VRX half against the live air: the monitor
#     interface is created, it is checked that it really is on a 5.8 GHz
#     channel and that it really receives the frames of the air, and the
#     production RX code is run on it (the production filter is installed on
#     a live monitor interface and the production parsing is exercised);
#   * with --tx it also runs the TX half, which is the real loopback. That
#     part is opt-in because the injection with the rtw88 drivers is expected
#     to fail (see above) and because the driver can fault on a frame that
#     it receives without a vif.
#
#
# The test needs:
#   - root (or CAP_NET_ADMIN + CAP_NET_RAW): the monitor mode interfaces and
#     the pcap capture / injection;
#   - the approval of the user (QUESTIONS.md Q8).
#
# Usage:
#   sudo -E ./code/r_tests/two_radio_rf_test.sh [--tx] [seconds] [channel]
#
# The channel is a 5.8 GHz channel (149 by default). The Eachine WiFiLink has
# 5 GHz-only antennas, so it must never be tuned to 2.4 GHz.

set -u

TX_USB_ID="2357:0120"     # TP-Link Archer T2U Plus  (the VTX side)
RX_USB_ID="0bda:881a"     # Eachine WiFiLink USB     (the VRX side)
TX_DESC="TP-Link Archer T2U Plus"
RX_DESC="Eachine WiFiLink USB adapter"
TX_MON="rubyvtx0"
RX_MON="rubyvrx0"
DURATION="60"
CHANNEL="149"
DO_TX=0
REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TEST_BIN="$REPO_DIR/test_video_rx_pipeline"
PLAYER_BIN="$REPO_DIR/ruby_player_desktop"
LOG_DIR="/tmp/ruby/two_radio_rf"

TX_IF=""
RX_IF=""
PLAYER_PID=""
PLAYER_WAS_RUNNING=0
TX_IF_WAS_UP=0
RX_IF_WAS_UP=0

say()  { printf '%s\n' "$*"; }
info() { printf '  %s\n' "$*"; }
fail() { printf '  ERROR: %s\n' "$*" >&2; }

# --- the arguments ------------------------------------------------------------
while [ $# -gt 0 ]; do
   case "$1" in
      --tx) DO_TX=1; shift ;;
      --help|-h)
         say "Usage: sudo -E $0 [--tx] [seconds] [channel]"
         say "  --tx    also run the TX half (the real loopback). Without it"
         say "          only the VRX half is verified against the live air."
         exit 0 ;;
      *) break ;;
   esac
done
if [ $# -gt 0 ]; then DURATION="$1"; shift; fi
if [ $# -gt 0 ]; then CHANNEL="$1"; shift; fi

# The interface of a USB device, found by the USB hardware ID.
netdev_for_usb_id()
{
   local id="$1" netdev dev
   for netdev in /sys/class/net/*; do
      [ -e "$netdev" ] || continue
      dev="$(readlink -f "$netdev/device/.." 2>/dev/null)"
      [ -r "$dev/idVendor" ] || continue
      if [ "$(cat "$dev/idVendor" 2>/dev/null):$(cat "$dev/idProduct" 2>/dev/null)" = "$id" ]; then
         basename "$netdev"
         return 0
      fi
   done
   return 1
}

# The default route interface: it must never be touched.
default_route_if()
{
   ip -o route show default 2>/dev/null | awk '{for(i=1;i<NF;i++) if ($i=="dev") {print $(i+1); exit}}'
}

# The frequency (in MHz) of an interface, 0 if it is not known.
# A monitor interface shows "channel 149 (5745 MHz), width: 80 MHz", a
# managed one that is up shows the same line, one that is down shows
# nothing at all.
interface_frequency()
{
   local out freq
   out="$(iw dev "$1" info 2>/dev/null)"
   freq="$(echo "$out" | sed -n 's/.*channel [0-9][0-9]* (\([0-9][0-9]*\) MHz).*/\1/p' | head -1)"
   if [ -z "$freq" ]; then
      freq="$(echo "$out" | sed -n 's/^[[:space:]]*freq \([0-9][0-9]*\).*/\1/p' | head -1)"
   fi
   [ -n "$freq" ] && echo "$freq" || echo 0
}

# The interface is administratively up (the UP flag), or not.
interface_is_admin_up()
{
   local flags
   flags="$(ip -o link show "$1" 2>/dev/null | sed -n 's/.*<\([^>]*\)>.*/\1/p')"
   case ",$flags," in
      *,UP,*) return 0 ;;
   esac
   return 1
}

# The number of the frames that the interface received from the air.
interface_rx_packets()
{
   local file="/sys/class/net/$1/statistics/rx_packets"
   if [ -r "$file" ]; then cat "$file" 2>/dev/null || echo 0; else echo 0; fi
}

# Deletes a monitor interface.
#
# The monitor interface of a driver without monitor vif support (the rtw88
# drivers of these two radios) is the "software monitor" of mac80211: it can
# NOT be deleted with "ip link del" (the kernel answers "Operation not
# supported"), it has to be deleted with "iw dev <name> del". The "ip link
# del" is kept as a fallback, for the drivers whose monitor interface is a
# real interface of the driver.
delete_monitor()
{
   local mon="$1"
   iw dev "$mon" del >/dev/null 2>&1
   ip link del "$mon" >/dev/null 2>&1
}

cleanup()
{
   if [ -n "$PLAYER_PID" ] && kill -0 "$PLAYER_PID" 2>/dev/null; then
      kill -TERM "$PLAYER_PID" 2>/dev/null
      wait "$PLAYER_PID" 2>/dev/null
   fi
   delete_monitor "$TX_MON"
   delete_monitor "$RX_MON"
   # The managed interfaces are put back in the state in which they were.
   [ "$TX_IF_WAS_UP" = "1" ] && ip link set "$TX_IF" up 2>/dev/null
   [ "$RX_IF_WAS_UP" = "1" ] && ip link set "$RX_IF" up 2>/dev/null
   # The radios are left on the 5.8 GHz channel on purpose: it is the only
   # band that the antennas of the $RX_DESC support, so they are never put
   # back on a 2.4 GHz channel.
}
trap cleanup EXIT INT TERM

say "================================================================"
say " RubyFPV two-radio RF test"
say "================================================================"

# --- the privileges ---------------------------------------------------------
if [ "$(id -u)" != "0" ]; then
   fail "this test needs root (or CAP_NET_ADMIN + CAP_NET_RAW)."
   if [ "$DO_TX" = "1" ]; then
      info "Run it as: sudo -E $0 --tx $DURATION $CHANNEL"
   else
      info "Run it as: sudo -E $0 $DURATION $CHANNEL"
   fi
   exit 1
fi

# --- the radios -------------------------------------------------------------
TX_IF="$(netdev_for_usb_id "$TX_USB_ID")"
if [ -z "$TX_IF" ]; then
   fail "the $TX_DESC (USB $TX_USB_ID) was not found."
   exit 1
fi
RX_IF="$(netdev_for_usb_id "$RX_USB_ID")"
if [ -z "$RX_IF" ]; then
   fail "the $RX_DESC (USB $RX_USB_ID) was not found."
   exit 1
fi

# The guard: the network connection of the machine is never used.
NET_IF="$(default_route_if)"
if [ "$TX_IF" = "wlan0" ] || [ "$RX_IF" = "wlan0" ]; then
   fail "wlan0 is the network connection of the machine, it is never used by this test."
   exit 1
fi
if [ -n "$NET_IF" ] && { [ "$TX_IF" = "$NET_IF" ] || [ "$RX_IF" = "$NET_IF" ]; }; then
   fail "the interface $NET_IF carries the default route, it is never used by this test."
   exit 1
fi

say "  The VTX radio: $TX_IF ($TX_DESC, USB $TX_USB_ID)"
say "  The VRX radio: $RX_IF ($RX_DESC, USB $RX_USB_ID)"
say "  The network interface (never touched): ${NET_IF:-none}"
say "  The channel: $CHANNEL (5.8 GHz band)"

# The 5 GHz-only antennas of the Eachine: it is never tuned to 2.4 GHz.
if [ "$CHANNEL" -lt 32 ] 2>/dev/null; then
   fail "channel $CHANNEL is in the 2.4 GHz band. The $RX_DESC has 5 GHz-only antennas."
   exit 1
fi

if [ ! -x "$TEST_BIN" ]; then
   fail "the test binary was not found: $TEST_BIN"
   info "Build it with: RUBY_BUILD_ENV=desktop make -j20 test_video_rx_pipeline"
   exit 1
fi

# --- the monitor mode interfaces --------------------------------------------
say ""
say "--- The monitor mode interfaces ---"
info "The monitor interface is created on the radio itself, with iw. The"
info "managed interface of the radio is brought down first: the channel of a"
info "monitor interface can only be set while no other interface of the"
info "radio is up (the kernel refuses it with EBUSY otherwise)."

setup_monitor()
{
   local base="$1" mon="$2" desc="$3" freq

   delete_monitor "$mon"

   # The state of the managed interface, to restore it at the end.
   if interface_is_admin_up "$base"; then
      eval "${4}=1"
   fi

   # The managed interface is brought down: the kernel refuses to change the
   # channel of a monitor interface while another interface of the same
   # radio is up.
   ip link set "$base" down 2>/dev/null

   # A monitor interface is created on the current channel of the radio, and
   # the default channel of these cards is a 2.4 GHz one. The radio is moved
   # to the 5.8 GHz channel first, so that the monitor interface is born on
   # the right band: the Eachine has 5 GHz-only antennas and the video link
   # of the models is a 5.8 GHz one.
   if iw dev "$base" set channel "$CHANNEL" 2>/dev/null; then
      info "$base was moved to the channel $CHANNEL before the monitor interface was created"
   else
      info "the radio $base could not be moved to the channel $CHANNEL before the monitor"
      info "interface was created: the monitor is born on the default channel of the"
      info "radio and it is moved to the channel $CHANNEL right after."
   fi

   if ! iw dev "$base" interface add "$mon" type monitor 2>/dev/null; then
      fail "the monitor interface could not be created on $base."
      info "mac80211 provides the monitor interface by itself (the software"
      info "monitor): the driver only has to deliver the frames. The other way"
      info "out is an adapter whose driver supports the monitor vif (the mt76"
      info "family) or the out-of-tree rtl8812au driver (DKMS), which needs a"
      info "new approval of the user."
      return 1
   fi

   ip link set "$mon" up 2>/dev/null

   if ! iw dev "$mon" set channel "$CHANNEL" 2>/dev/null; then
      fail "the channel $CHANNEL could not be set on $mon."
      delete_monitor "$mon"
      return 1
   fi

   # The frequency is checked right after the change: the radios must be on
   # the 5.8 GHz band (the Eachine has 5 GHz-only antennas, so it must never
   # stay on a 2.4 GHz channel).
   freq="$(interface_frequency "$mon")"
   if [ "$freq" -lt 5000 ] 2>/dev/null; then
      fail "the $desc is on ${freq} MHz, not on a 5.8 GHz channel."
      info "The 5.8 GHz band is the reference band of the video link and the"
      info "Eachine has 5 GHz-only antennas: the monitor interface is deleted"
      info "and the test is stopped here."
      delete_monitor "$mon"
      return 1
   fi

   info "$mon (on $base) is up, on channel $CHANNEL ($freq MHz)"
   return 0
}

setup_monitor "$RX_IF" "$RX_MON" "$RX_DESC" RX_IF_WAS_UP || exit 1

if [ "$DO_TX" = "1" ]; then
   setup_monitor "$TX_IF" "$TX_MON" "$TX_DESC" TX_IF_WAS_UP || exit 1
else
   info "$TX_IF is left alone (the TX half was not requested)"
fi

# --- the capture of the live air --------------------------------------------
say ""
say "--- The capture of the live air ---"
RX_PACKETS_BEFORE="$(interface_rx_packets "$RX_MON")"
info "the frames received by $RX_MON before the test: $RX_PACKETS_BEFORE"
sleep 3
RX_PACKETS_AFTER="$(interface_rx_packets "$RX_MON")"
info "the frames received by $RX_MON now: $RX_PACKETS_AFTER"
if [ "$RX_PACKETS_AFTER" -gt "$RX_PACKETS_BEFORE" ] 2>/dev/null; then
   info "the monitor interface receives the frames of the air: the capture works."
   CAPTURE_WORKS=1
else
   info "no frame was received in 3 seconds (there may be nothing on that"
   info "channel): this does not prove that the capture is broken."
   CAPTURE_WORKS=0
fi

# --- the desktop player -----------------------------------------------------
mkdir -p "$LOG_DIR"
say ""
say "--- The desktop player ---"
if pgrep -f "ruby_player_desktop" >/dev/null 2>&1; then
   PLAYER_WAS_RUNNING=1
   info "the player is already running"
else
   if [ ! -x "$PLAYER_BIN" ]; then
      fail "the player was not found: $PLAYER_BIN"
      exit 1
   fi
   # The player is never run as root: it is started as the user of the
   # desktop session.
   RUN_USER="${SUDO_USER:-}"
   if [ -z "$RUN_USER" ] && [ -n "${XDG_RUNTIME_DIR:-}" ] && [ -d "${XDG_RUNTIME_DIR}" ]; then
      # The session that runs the script is not a sudo one: the owner of the
      # runtime directory of the session is the user of the desktop.
      RUN_USER="$(stat -c %U "${XDG_RUNTIME_DIR}" 2>/dev/null)"
   fi
   if [ -z "$RUN_USER" ] || [ "$RUN_USER" = "root" ]; then
      fail "the player has to be started by the user of the desktop session."
      info "Start it with: $PLAYER_BIN -p"
      info "and run this script again."
      exit 1
   fi
   notify-send "RubyFPV" "The two-radio RF test starts the video player." 2>/dev/null
   RUN_HOME="$(getent passwd "$RUN_USER" 2>/dev/null | cut -d: -f6)"
   [ -z "$RUN_HOME" ] && RUN_HOME="/home/$RUN_USER"
   # The player is started with the privileges of the user of the desktop
   # session, never with the ones of root.
   if command -v runuser >/dev/null 2>&1; then
      runuser -u "$RUN_USER" -- env DISPLAY="${DISPLAY:-}" WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-}" \
         XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-}" HOME="$RUN_HOME" \
         "$PLAYER_BIN" -p >"$LOG_DIR/player.log" 2>&1 &
   else
      sudo -u "$RUN_USER" env DISPLAY="${DISPLAY:-}" WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-}" \
         XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-}" HOME="$RUN_HOME" \
         "$PLAYER_BIN" -p >"$LOG_DIR/player.log" 2>&1 &
   fi
   PLAYER_PID=$!
   sleep 2
   info "the player was started (pid $PLAYER_PID)"
fi

# --- the test ---------------------------------------------------------------
cd "$REPO_DIR" || exit 1

if [ "$DO_TX" = "1" ]; then
   say ""
   say "--- The RF loopback ($DURATION seconds) ---"
   info "The TX half injects the real video frames on $TX_MON."
   info "With the in-tree rtw88 drivers mac80211 drops the injected frames"
   info "before they reach the radio (the driver has no monitor vif), so the"
   info "VRX half is expected to receive nothing. That is the answer of the"
   info "test, not a fault of the production code."

   "$TEST_BIN" --rf-rx "$DURATION" "$RX_MON" >"$LOG_DIR/rx.log" 2>&1 &
   RX_PID=$!
   sleep 2
   "$TEST_BIN" --rf-tx "$DURATION" "$TX_MON" >"$LOG_DIR/tx.log" 2>&1
   TX_STATUS=$?
   wait $RX_PID
   RX_STATUS=$?
else
   say ""
   say "--- The VRX half against the live air ($DURATION seconds) ---"
   info "The production RX code runs on the live monitor interface $RX_MON:"
   info "the production filter is installed on it and the production parsing"
   info "is exercised. Without the TX half no RubyFPV frame is on the air,"
   info "so no video packet is expected."
   "$TEST_BIN" --rf-rx "$DURATION" "$RX_MON" >"$LOG_DIR/rx.log" 2>&1
   RX_STATUS=$?
   TX_STATUS=0
fi

say ""
say "--- The results ---"
if [ "$DO_TX" = "1" ]; then
   tail -n 6 "$LOG_DIR/tx.log"
fi
tail -n 8 "$LOG_DIR/rx.log"
info "the logs are in $LOG_DIR"

RX_PACKETS_END="$(interface_rx_packets "$RX_MON")"
info "the frames received by $RX_MON during the test: $RX_PACKETS_END"

# The status of the VRX half: 0 = the video packets were received and they
# reached the video pipeline, 1 = the interface was read but no video packet
# arrived, 255 = the interface could not be opened at all.
if [ "$RX_STATUS" = "0" ]; then
   RX_VERDICT="the video packets were received and they reached the video pipeline"
elif [ "$RX_STATUS" = "1" ]; then
   RX_VERDICT="the interface was read, but no video packet arrived"
else
   RX_VERDICT="the interface could not be read at all"
fi

if [ "$DO_TX" = "1" ]; then
   if [ "$RX_STATUS" = "0" ]; then
      say ""
      say "  The video travelled over the air, through the production receive"
      say "  path, to the desktop player."
      exit 0
   fi
   say ""
   say "  The VRX half did not receive the video frames ($RX_VERDICT)."
   if [ "$TX_STATUS" = "0" ]; then
      info "The TX half did not report an error: pcap_inject() accepted the"
      info "frames, but they did not reach the air. With the in-tree rtw88"
      info "drivers this is the expected result: the driver announces no"
      info "monitor vif (IEEE80211_HW_WANT_MONITOR_VIF), no queue control"
      info "and no active monitor, so mac80211 drops the injected frames"
      info "before they reach the radio. The TX half needs an adapter whose"
      info "driver supports the monitor vif (the mt76 family)."
   fi
   exit 1
fi

if [ "$RX_STATUS" = "255" ]; then
   fail "the VRX half could not open the live monitor interface (status: $RX_STATUS)."
   info "The production RX code has to open the live monitor interface and"
   info "to install the production filter on it. The log says why it failed."
   exit 1
fi

say ""
say "  The VRX half is verified against the live air: the production filter"
say "  is installed on a real monitor interface and the production parsing"
say "  runs on it."
if [ "$CAPTURE_WORKS" = "1" ]; then
   say "  The monitor interface receives the frames of the air."
fi
if [ "$RX_STATUS" = "1" ]; then
   say "  No video packet arrived, and that is the expected result: the TX"
   say "  half was not requested, so there was no RubyFPV traffic on the air."
fi
exit 0
