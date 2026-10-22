#!/usr/bin/env bash
#
# Switch the TP-Link Archer T2U Plus to the out-of-tree driver.
#
# The script unloads the in-tree driver of the adapter (rtw88_8821au) and
# loads the out-of-tree one (88XXau), so that the adapter can inject the
# RubyFPV frames in monitor mode.
#
# The adapter is identified by its USB hardware ID (2357:0120), never by its
# interface name. The script refuses to run if that adapter is the one that
# carries the default route of the machine, so the network connection
# (wlan0, the Intel AX210) cannot be touched by accident.
#
# The switch is reversible: restore_in_tree_driver.sh puts the in-tree driver
# back.
#
# It needs the root privileges.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

require_root

PERSIST="no"
for arg in "$@"; do
   case "${arg}" in
      --persist) PERSIST="yes" ;;
      *) log_error "Unknown option: ${arg}"; exit 1 ;;
   esac
done

DEFAULT_IF="$(default_route_interface)"
log_info "The interface that carries the default route: ${DEFAULT_IF} (never touched)"

IFACE="$(safe_tx_interface "${DEFAULT_IF}")" || exit 1
log_info "The TP-Link adapter (${TX_USB_VENDOR}:${TX_USB_PRODUCT}) is ${IFACE}"
log_info "Its current driver: $(driver_of_interface "${IFACE}")"

# Any extra interface (a monitor interface, a virtual interface) that sits on
# the same adapter is removed first, so that the module can be unloaded.
for extra in $(ls /sys/class/net/ 2>/dev/null); do
   [ "${extra}" = "${IFACE}" ] && continue
   extra_id="$(usb_id_of_interface "${extra}" 2>/dev/null || true)"
   if [ "${extra_id}" = "${TX_USB_VENDOR}:${TX_USB_PRODUCT}" ]; then
      log_info "Removing the extra interface ${extra} of the same adapter ..."
      ip link set "${extra}" down 2>/dev/null || true
      ip link delete "${extra}" 2>/dev/null || true
   fi
done

ip link set "${IFACE}" down 2>/dev/null || true

if lsmod | grep -q "^${OUT_OF_TREE_MODULE} "; then
   log_info "The module ${OUT_OF_TREE_MODULE} is already loaded."
else
   log_info "Unloading the in-tree driver ${IN_TREE_MODULE} ..."
   if ! modprobe -r "${IN_TREE_MODULE}"; then
      log_error "The in-tree driver ${IN_TREE_MODULE} could not be unloaded."
      log_error "It is usually in use because the adapter is still bound to it:"
      log_error "  unplug the TP-Link adapter, wait a second, plug it back and run this script again."
      exit 1
   fi

   log_info "Loading the out-of-tree driver ${OUT_OF_TREE_MODULE} (${OOT_MODULE_PARAMS}) ..."
   if ! modprobe "${OUT_OF_TREE_MODULE}" ${OOT_MODULE_PARAMS}; then
      log_error "The module ${OUT_OF_TREE_MODULE} could not be loaded."
      log_error "The in-tree driver can be brought back with: modprobe ${IN_TREE_MODULE}"
      exit 1
   fi
fi

# Wait for the adapter to be enumerated again with the new driver.
NEW_IFACE=""
for _ in $(seq 1 20); do
   NEW_IFACE="$(interface_for_usb_id "${TX_USB_VENDOR}" "${TX_USB_PRODUCT}" 2>/dev/null || true)"
   [ -n "${NEW_IFACE}" ] && break
   sleep 0.5
done

if [ -z "${NEW_IFACE}" ]; then
   log_error "The adapter did not come back after the driver change."
   log_error "Check the kernel log: dmesg | tail -40"
   exit 1
fi

log_info "The adapter is back as ${NEW_IFACE}, with the driver $(driver_of_interface "${NEW_IFACE}")"
if [ "${NEW_IFACE}" != "${IFACE}" ]; then
   log_info "The interface name changed (${IFACE} -> ${NEW_IFACE}): this is why the radios are identified by their USB hardware ID."
fi

if [ "${PERSIST}" = "yes" ]; then
   BLACKLIST="/etc/modprobe.d/rubyfpv-88xxau-blacklist.conf"
   log_info "Blacklisting ${IN_TREE_MODULE} in ${BLACKLIST} (so that the adapter always binds to the out-of-tree driver) ..."
   cat > "${BLACKLIST}" <<EOF
# Written by the RubyFPV out-of-tree driver scripts.
# The TP-Link Archer T2U Plus (2357:0120) is used with the out-of-tree
# driver ${OUT_OF_TREE_MODULE}, that supports the monitor vif injection.
# Remove this file and run restore_in_tree_driver.sh to go back to the
# in-tree driver.
blacklist ${IN_TREE_MODULE}
EOF
fi

log_info ""
log_info "The switch is done. The monitor mode injection of RubyFPV is now available on ${NEW_IFACE}."
log_info "To go back to the in-tree driver: sudo ${SCRIPT_DIR}/restore_in_tree_driver.sh"
