#!/usr/bin/env bash
#
# Remove the out-of-tree driver from the system.
#
# The script removes the DKMS package (the module file and the source tree
# in /usr/src) and the optional blacklist. It does not touch the in-tree
# driver, that keeps working as before.
#
# It needs the root privileges.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

require_root

if lsmod | grep -q "^${OUT_OF_TREE_MODULE} "; then
   log_error "The module ${OUT_OF_TREE_MODULE} is loaded right now."
   log_error "Restore the in-tree driver first: sudo ${SCRIPT_DIR}/restore_in_tree_driver.sh"
   exit 1
fi

if dkms status -m "${DKMS_PACKAGE}" -v "${DKMS_VERSION}" 2>/dev/null | grep -q "${DKMS_PACKAGE}"; then
   log_info "Removing the DKMS package ${DKMS_PACKAGE}/${DKMS_VERSION} ..."
   dkms remove "${DKMS_PACKAGE}/${DKMS_VERSION}" --all || true
else
   log_info "The DKMS package ${DKMS_PACKAGE}/${DKMS_VERSION} is not registered."
fi

if [ -d "${DKMS_SRC_DIR}" ]; then
   log_info "Removing the source tree ${DKMS_SRC_DIR} ..."
   rm -rf "${DKMS_SRC_DIR}"
fi

BLACKLIST="/etc/modprobe.d/rubyfpv-88xxau-blacklist.conf"
if [ -f "${BLACKLIST}" ]; then
   log_info "Removing the blacklist ${BLACKLIST} ..."
   rm -f "${BLACKLIST}"
fi

depmod -a "$(uname -r)" || true

log_info "The out-of-tree driver has been removed from the system."
log_info "The in-tree driver of the adapter is untouched."
