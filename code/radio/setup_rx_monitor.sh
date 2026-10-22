#!/usr/bin/env bash
#
# Put a radio adapter in monitor mode, on a given frequency, with the
# privileges that the unprivileged desktop application cannot have.
#
# The RubyFPV desktop ground station reads the radio frames with libpcap, and
# libpcap only delivers the radiotap header of a frame when the adapter is in
# monitor mode. The application itself asks for monitor mode, but that needs
# CAP_NET_ADMIN, and the application is never run as root: the request fails
# with EPERM and the capture is opened on a managed interface, where the
# encapsulation is not the radiotap one and the video packets are never seen.
#
# This script performs the same operation with the root privileges, so that
# the application finds the interface already in monitor mode, and it can
# open the capture on it.
#
# It needs the root privileges:
#
#    sudo code/radio/setup_rx_monitor.sh [interface] [frequency in MHz]
#
# The defaults are the receiving adapter of the current test setup
# (wlan1, the Eachine WiFiLink) and the frequency of the current vehicle
# model (5825 MHz, channel 165).
#
# The script refuses to touch the adapter that carries the network
# connection, and it refuses the 2.4 GHz band on the cards that have 5 GHz
# only antennas, exactly like the application does.

set -euo pipefail

IFACE="${1:-wlan1}"
FREQ_MHZ="${2:-5825}"

# The card that has 5 GHz only antennas: the RTL8812AU dual antenna adapter
# (USB 0bda:881a, the Eachine WiFiLink). Tuning it to the 2.4 GHz band would
# damage its antennas. The same rule is in the application:
# radio_cap_card_model_is_5ghz_only() in code/base/radio_capabilities.cpp
# (card model CARD_MODEL_RTL8812AU_DUAL_ANTENNA).
FIVE_GHZ_ONLY_USB_ID="0bda:881a"

# The adapter that must never be touched: it carries the network connection
# of the machine. The name is only a fallback, the interface that holds the
# default route is detected at runtime.
PROTECTED_IFACE="wlan0"

die() { echo "ERROR: $*" >&2; exit 1; }

if [ "$(id -u)" != "0" ]; then
   die "This script needs the root privileges: sudo $0 ${IFACE} ${FREQ_MHZ}"
fi

if ! [ -d "/sys/class/net/${IFACE}" ]; then
   die "The interface ${IFACE} does not exist."
fi

# --- Safety: never touch the adapter that carries the network connection ---

carrier_iface="$(ip -o route show default 2>/dev/null | awk '{print $5}' | head -1)"
if [ "${carrier_iface}" = "${IFACE}" ]; then
   die "The interface ${IFACE} carries the default route: it is the network connection of this machine and it must not be modified."
fi

if [ "${IFACE}" = "${PROTECTED_IFACE}" ]; then
   die "The interface ${IFACE} is the protected one: it must not be modified."
fi

# --- Safety: 5 GHz only antennas cannot be tuned to the 2.4 GHz band ---

usb_id="$(udevadm info -q property -p "/sys/class/net/${IFACE}" 2>/dev/null | awk -F= '/^ID_VENDOR_ID=/{v=$2} /^ID_MODEL_ID=/{m=$2} END{print (v!="" && m!="") ? tolower(v ":" m) : ""}')"

if [ "${usb_id}" = "${FIVE_GHZ_ONLY_USB_ID}" ]; then
   if [ "${FREQ_MHZ}" -lt 5000 ]; then
      die "The adapter ${IFACE} (USB ${usb_id}) has 5 GHz only antennas: it cannot be tuned to ${FREQ_MHZ} MHz."
   fi
fi

# --- The frequency has to be one that the adapter supports ---

PHY_NAME="$(basename "$(readlink -f "/sys/class/net/${IFACE}/phy80211")")"

if ! iw phy "${PHY_NAME}" info 2>/dev/null | grep -qF "${FREQ_MHZ}"; then
   die "The frequency ${FREQ_MHZ} MHz is not in the channel list of ${IFACE}."
fi

echo "Setting up ${IFACE} in monitor mode on ${FREQ_MHZ} MHz ..."

# NetworkManager must not touch the interface any more, otherwise it puts it
# back to managed mode and it reconfigures it.
if command -v nmcli >/dev/null 2>&1; then
   nmcli device set "${IFACE}" managed no 2>/dev/null || true
fi

# Preconfigured interfaces need no RTNETLINK/IP writes, which are denied to
# unprivileged helper processes on some desktop kernels. Only apply them when
# the interface is not already in monitor mode on the requested frequency.
if ! iw dev "${IFACE}" info 2>/dev/null | grep -qE 'type monitor|channel[^0-9]*'"${FREQ_MHZ}"'([^0-9]|$)'; then
   ip link set dev "${IFACE}" down
   iw dev "${IFACE}" set type monitor
   ip link set dev "${IFACE}" up
fi
iw dev "${IFACE}" set freq "${FREQ_MHZ}" || true

echo ""
echo "Result:"
iw dev "${IFACE}" info | grep -E 'Interface|type|channel|freq'
echo ""
echo "The RubyFPV application can now open this interface for the video capture."
