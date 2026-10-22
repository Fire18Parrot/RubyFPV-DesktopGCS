#!/usr/bin/env bash
#
# Install the out-of-tree driver with DKMS.
#
# The script copies the prepared source tree to /usr/src, registers it with
# DKMS, builds it and installs it for the running kernel. The module lands
# in /lib/modules/$(uname -r)/updates/88XXau.ko, where modprobe finds it.
#
# The script does NOT change the running driver: the module is only built
# and installed here. The switch to the out-of-tree driver, and the way back
# to the in-tree one, are the separate scripts use_out_of_tree_driver.sh and
# restore_in_tree_driver.sh.
#
# It needs the root privileges.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

require_root

SRC_DIR="${RUBYFPV_DRIVER_WORKDIR}/rtl8812au"
KVER="$(uname -r)"

if [ ! -d "${SRC_DIR}" ]; then
   log_error "The source tree is missing: ${SRC_DIR} (run prepare_source.sh first)."
   exit 1
fi

if lsmod | grep -q "^${OUT_OF_TREE_MODULE} "; then
   log_error "The module ${OUT_OF_TREE_MODULE} is loaded right now."
   log_error "Restore the in-tree driver first: sudo ${SCRIPT_DIR}/restore_in_tree_driver.sh"
   exit 1
fi

# A DKMS package that is already there is removed first, so that the
# installation is always the one of the current source tree.
if dkms status -m "${DKMS_PACKAGE}" -v "${DKMS_VERSION}" 2>/dev/null | grep -q "${DKMS_PACKAGE}"; then
   log_info "Removing the previous DKMS package ${DKMS_PACKAGE}/${DKMS_VERSION} ..."
   dkms remove "${DKMS_PACKAGE}/${DKMS_VERSION}" --all || true
fi

log_info "Copying the source tree to ${DKMS_SRC_DIR} ..."
rm -rf "${DKMS_SRC_DIR}"
mkdir -p "${DKMS_SRC_DIR}"
tar -C "${SRC_DIR}" --exclude='./.git' --exclude='*.o' --exclude='*.ko' --exclude='*.mod' --exclude='*.mod.c' --exclude='./Module.symvers' -cf - . | tar -C "${DKMS_SRC_DIR}" -xf -

# The DKMS configuration of the upstream repository declares another package
# name and another version, so it is replaced here with the one that this
# script uses, to keep "dkms status" and the removal path unambiguous.
#
# The module has to be built with the same compiler as the kernel: a kernel
# that was built with clang passes clang only flags to the modules and gcc
# refuses them, so the build gets LLVM=1 when the kernel is a clang one.
DKMS_MAKE_EXTRA=""
if grep -q "^CONFIG_CLANG_VERSION=" "/lib/modules/${KVER}/build/.config" 2>/dev/null; then
   DKMS_MAKE_EXTRA=" LLVM=1"
   log_info "The kernel was built with clang: the module is built with LLVM=1 too."
fi

cat > "${DKMS_SRC_DIR}/dkms.conf" <<EOF
PACKAGE_NAME="${DKMS_PACKAGE}"
PACKAGE_VERSION="${DKMS_VERSION}"
CLEAN="'make' clean"
BUILT_MODULE_NAME[0]=${OUT_OF_TREE_MODULE}
PROCS_NUM=\`nproc\`
[ \$PROCS_NUM -gt 16 ] && PROCS_NUM=16
DEST_MODULE_LOCATION[0]="/updates"
MAKE="'make' -j\$PROCS_NUM KVER=\${kernelver} KSRC=/lib/modules/\${kernelver}/build${DKMS_MAKE_EXTRA}"
AUTOINSTALL="yes"
REMAKE_INITRD="no"
EOF

log_info "DKMS add ${DKMS_PACKAGE}/${DKMS_VERSION} ..."
dkms add -m "${DKMS_PACKAGE}" -v "${DKMS_VERSION}"

log_info "DKMS build for ${KVER} (this takes a few minutes) ..."
dkms build -m "${DKMS_PACKAGE}" -v "${DKMS_VERSION}" -k "${KVER}"

log_info "DKMS install ..."
dkms install -m "${DKMS_PACKAGE}" -v "${DKMS_VERSION}" -k "${KVER}"

depmod -a "${KVER}" || true

log_info "DKMS status:"
dkms status -m "${DKMS_PACKAGE}" -v "${DKMS_VERSION}"

INSTALLED="/lib/modules/${KVER}/updates/${OUT_OF_TREE_MODULE}.ko"
if [ -r "${INSTALLED}" ]; then
   log_info "The module is installed: ${INSTALLED}"
else
   log_warn "The module file was not found at ${INSTALLED}. Check the DKMS status above."
fi

log_info ""
log_info "The running driver has NOT been changed yet."
log_info "To use the out-of-tree driver:  sudo ${SCRIPT_DIR}/use_out_of_tree_driver.sh"
log_info "To go back to the in-tree one:  sudo ${SCRIPT_DIR}/restore_in_tree_driver.sh"
