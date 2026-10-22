#!/usr/bin/env bash
#
# Bring back the in-tree driver of the TP-Link Archer T2U Plus.
#
# The script unloads the out-of-tree driver (88XXau) and loads the in-tree
# one (rtw88_8821au), so that the adapter goes back to the state it had
# before the out-of-tree driver was used.
#
# The adapter is identified by its USB hardware ID (2357:0120) and the
# script refuses to run if that adapter carries the default route of the
# machine, so the network connection (wlan0, the Intel AX210) cannot be
# touched by accident.
#
# It needs the root privileges.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

require_root

DEFAULT_IF="$(default_route_interface)"
log_info "The interface that carries the default route: ${DEFAULT_IF} (never touched)"

IFACE="$(safe_tx_interface "${DEFAULT_IF}")" || exit 1
log_info "The TP-Link adapter (${TX_USB_VENDOR}:${TX_USB_PRODUCT}) is ${IFACE}"
log_info "Its current driver: $(driver_of_interface "${IFACE}")"

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
   log_info "Unloading the out-of-tree driver ${OUT_OF_TREE_MODULE} ..."
   if ! modprobe -r "${OUT_OF_TREE_MODULE}"; then
      log_error "The module ${OUT_OF_TREE_MODULE} could not be unloaded."
      log_error "Unplug the TP-Link adapter, wait a second, plug it back and run this script again."
      exit 1
   fi
else
   log_info "The module ${OUT_OF_TREE_MODULE} is not loaded."
fi

log_info "Loading the in-tree driver ${IN_TREE_MODULE} ..."
if ! modprobe "${IN_TREE_MODULE}"; then
   log_error "The in-tree driver ${IN_TREE_MODULE} could not be loaded."
   log_error "Check the kernel log: dmesg | tail -40"
   exit 1
fi

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

BLACKLIST="/etc/modprobe.d/rubyfpv-88xxau-blacklist.conf"
if [ -f "${BLACKLIST}" ]; then
   log_info "Removing the blacklist ${BLACKLIST} ..."
   rm -f "${BLACKLIST}"
fi

log_info ""
log_info "The in-tree driver is back. The out-of-tree module is still installed, but it is not loaded:"
log_info "  it can be removed with: sudo ${SCRIPT_DIR}/remove_module.sh"
