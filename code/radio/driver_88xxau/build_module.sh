#!/usr/bin/env bash
#
# Compile the out-of-tree driver against the headers of the running kernel.
#
# This script does not install anything and it does not change the state of
# the system: it only produces the module file 88XXau.ko in the source tree,
# as a proof that the driver builds on this kernel. The installation is a
# separate step (install_module.sh), and it needs the root privileges.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

SRC_DIR="${RUBYFPV_DRIVER_WORKDIR}/rtl8812au"
KSRC="/lib/modules/$(uname -r)/build"

if [ ! -d "${SRC_DIR}" ]; then
   log_error "The source tree is missing: ${SRC_DIR} (run prepare_source.sh first)."
   exit 1
fi

if [ ! -d "${KSRC}" ]; then
   log_error "The kernel headers are missing: ${KSRC}"
   log_error "Install the matching linux-headers package before building."
   exit 1
fi

log_info "Building ${OUT_OF_TREE_MODULE}.ko for kernel $(uname -r) ..."
log_info "  source: ${SRC_DIR}"
log_info "  headers: ${KSRC}"

JOBS="$(nproc)"
if [ "${JOBS}" -gt 16 ]; then JOBS=16; fi

# The module has to be built with the same compiler as the kernel. A kernel
# that was built with clang passes clang only flags to the modules
# (-flto=thin -fsplit-lto-unit, and the AutoFDO ones: -mllvm,
# -improved-fs-discriminator), and gcc refuses them. The CachyOS kernel of
# this machine is a clang one (CONFIG_CLANG_VERSION in the kernel config),
# so the module is built with LLVM=1, that makes the kernel build system use
# clang, ld.lld and the llvm binutils, exactly as the kernel was built.
BUILD_ARGS=()
if grep -q "^CONFIG_CLANG_VERSION=" "${KSRC}/.config" 2>/dev/null; then
   if ! command -v clang >/dev/null 2>&1; then
      log_error "The kernel was built with clang, but clang is not installed."
      exit 1
   fi
   log_info "The kernel was built with clang ($(clang --version | head -1)), the module is built with LLVM=1 too."
   BUILD_ARGS+=("LLVM=1")
fi

cd "${SRC_DIR}"
make -j"${JOBS}" "${BUILD_ARGS[@]}" KSRC="${KSRC}" 2>&1 | tee "${RUBYFPV_DRIVER_WORKDIR}/build.log"

MODULE_PATH="$(find "${SRC_DIR}" -maxdepth 2 -name "${OUT_OF_TREE_MODULE}.ko" | head -1)"
if [ -z "${MODULE_PATH}" ]; then
   log_error "The build finished without producing ${OUT_OF_TREE_MODULE}.ko."
   log_error "The errors are in ${RUBYFPV_DRIVER_WORKDIR}/build.log"
   exit 1
fi

log_info "Build OK: ${MODULE_PATH}"
log_info "Module information:"
modinfo "${MODULE_PATH}" 2>/dev/null | grep -E '^(filename|version|license|description|alias)' | head -20
log_info ""
log_info "The module has NOT been installed and the running driver has NOT been changed."
log_info "Next step (needs the root): sudo ${SCRIPT_DIR}/install_module.sh"
