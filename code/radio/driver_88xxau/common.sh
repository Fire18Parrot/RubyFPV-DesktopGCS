#!/usr/bin/env bash
#
# Shared definitions and helpers for the out-of-tree driver scripts.
#
# The helpers here are the safety core of the whole directory: every script
# that changes the module state refuses to act on an interface that carries
# the network connection of the machine, and identifies the TP-Link adapter
# by its USB hardware ID (2357:0120) instead of by its interface name.

# The TP-Link Archer T2U Plus (RTL8821AU), the intended VTX radio.
readonly TX_USB_VENDOR="2357"
readonly TX_USB_PRODUCT="0120"

# The out-of-tree driver.
readonly OUT_OF_TREE_REPO="https://github.com/aircrack-ng/rtl8812au.git"
readonly OUT_OF_TREE_COMMIT="734485506a30d6237c2deaad666a19f8ca5379f2"
readonly OUT_OF_TREE_BRANCH="v5.6.4.2"
readonly OUT_OF_TREE_MODULE="88XXau"
readonly OUT_OF_TREE_USB_DRIVER="rtl8812au"

# The DKMS package that carries the out-of-tree module.
readonly DKMS_PACKAGE="rubyfpv-88xxau"
readonly DKMS_VERSION="5.6.4.2-rubyfpv1"
readonly DKMS_SRC_DIR="/usr/src/${DKMS_PACKAGE}-${DKMS_VERSION}"

# The in-tree driver of the same adapter, the one that is restored when the
# out-of-tree driver is not wanted any more.
readonly IN_TREE_MODULE="rtw88_8821au"

# The module parameters that RubyFPV needs on the out-of-tree driver.
#
# rtw_monitor_disable_1m=1 is mandatory for the 5.8 GHz band: with the
# default (0) the driver forces every injected frame to 1 Mbps, and 1 Mbps
# does not exist on the 5 GHz channels. The frames would be accepted by the
# driver and never reach the air.
readonly OOT_MODULE_PARAMS="rtw_monitor_disable_1m=1"

# The work directory of the scripts. The source tree is kept out of the
# project tree, under /tmp/ruby/, as for the other RubyFPV work files.
readonly DEFAULT_WORKDIR="/tmp/ruby/driver_88xxau"

RUBYFPV_DRIVER_WORKDIR="${RUBYFPV_DRIVER_WORKDIR:-${DEFAULT_WORKDIR}}"
readonly RUBYFPV_DRIVER_WORKDIR

# The patch that ports the driver to the Linux 7.x kernel API. It lives next
# to this file, in the project tree.
readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly PATCH_FILE="${SCRIPT_DIR}/88xxau-linux-7.2.patch"

log_info()  { echo "[driver_88xxau] $*"; }
log_warn()  { echo "[driver_88xxau] WARNING: $*" >&2; }
log_error() { echo "[driver_88xxau] ERROR: $*" >&2; }

require_root()
{
   if [ "$(id -u)" != "0" ]; then
      log_error "This script changes the module state and must be run as root (use sudo)."
      exit 1
   fi
}

# The interface that carries the default route of the machine. This is the
# interface that must never be touched: on this machine it is wlan0, the
# Intel AX210 that holds the network connection.
default_route_interface()
{
   ip -o route show default 2>/dev/null | awk '{ for (i=1;i<NF;i++) if ($i=="dev") { print $(i+1); exit } }'
}

# The interface name of the adapter with the given USB hardware ID, or the
# empty string if the adapter is not plugged in. The identification is done
# with the USB hardware ID and never with the interface name, because the
# interface name is not stable across a module reload.
interface_for_usb_id()
{
   local vendor="$1" product="$2" netdev
   for netdev in /sys/class/net/*; do
      [ -e "${netdev}" ] || continue
      local uevent="${netdev}/device/uevent"
      [ -r "${uevent}" ] || continue
      if grep -qi "^PRODUCT=${vendor}/${product}/" "${uevent}" 2>/dev/null; then
         basename "${netdev}"
         return 0
      fi
   done
   return 1
}

# The USB hardware ID of the interface, as "vendor:product", or the empty
# string.
usb_id_of_interface()
{
   local iface="$1" uevent="/sys/class/net/${1}/device/uevent"
   [ -r "${uevent}" ] || return 1
   local product vendor
   product="$(grep -i '^PRODUCT=' "${uevent}" 2>/dev/null | cut -d= -f2 | cut -d/ -f2)"
   vendor="$(grep -i '^PRODUCT=' "${uevent}" 2>/dev/null | cut -d= -f2 | cut -d/ -f1)"
   [ -n "${vendor}" ] && [ -n "${product}" ] || return 1
   echo "${vendor}:${product}"
}

# The USB driver bound to the interface, as the kernel reports it in the
# uevent of the USB interface.
driver_of_interface()
{
   local iface="$1" uevent="/sys/class/net/${1}/device/uevent"
   [ -r "${uevent}" ] || return 1
   grep -i '^DRIVER=' "${uevent}" 2>/dev/null | cut -d= -f2
}

# The safety gate of every script that changes the state of a radio: the
# script refuses to act if the adapter that it is about to touch carries the
# default route of the machine, and it refuses to act on the interface that
# is named by the caller as forbidden.
#
# It returns the name of the adapter interface in the standard output.
assert_safe_target()
{
   local iface="$1"
   local forbidden="$2"

   if [ -z "${iface}" ]; then
      log_error "The adapter ${TX_USB_VENDOR}:${TX_USB_PRODUCT} is not present. Nothing to do."
      return 1
   fi

   if [ "${iface}" = "${forbidden}" ]; then
      log_error "Refusing to touch ${iface}: it is the forbidden interface of this machine."
      return 1
   fi

   local default_if
   default_if="$(default_route_interface)"
   if [ -n "${default_if}" ] && [ "${default_if}" = "${iface}" ]; then
      log_error "Refusing to touch ${iface}: it carries the default route of the machine."
      return 1
   fi

   local id
   id="$(usb_id_of_interface "${iface}")"
   if [ "${id}" != "${TX_USB_VENDOR}:${TX_USB_PRODUCT}" ]; then
      log_error "Refusing to touch ${iface}: its USB hardware ID is '${id}', not ${TX_USB_VENDOR}:${TX_USB_PRODUCT}."
      return 1
   fi

   echo "${iface}"
   return 0
}

# The name of the adapter interface, with the safety gate applied.
safe_tx_interface()
{
   local forbidden="${1}"
   local iface
   iface="$(interface_for_usb_id "${TX_USB_VENDOR}" "${TX_USB_PRODUCT}")" || true
   [ -n "${iface}" ] || return 1
   assert_safe_target "${iface}" "${forbidden}"
}
