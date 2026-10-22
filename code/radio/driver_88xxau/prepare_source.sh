#!/usr/bin/env bash
#
# Prepare the source tree of the out-of-tree driver.
#
# It clones the aircrack-ng/rtl8812au repository at the pinned commit and
# applies the RubyFPV patch that ports the driver to the Linux 7.x kernel
# API. It needs no root and it changes nothing outside the work directory.
#
# The result is a source tree that can be compiled with build_module.sh.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

WORKDIR="${RUBYFPV_DRIVER_WORKDIR}"
SRC_DIR="${WORKDIR}/rtl8812au"

if [ ! -r "${PATCH_FILE}" ]; then
   log_error "The port patch is missing: ${PATCH_FILE}"
   exit 1
fi

if [ ! -d "${SRC_DIR}/.git" ]; then
   mkdir -p "${WORKDIR}"
   log_info "Cloning ${OUT_OF_TREE_REPO} into ${SRC_DIR} ..."
   git clone --quiet "${OUT_OF_TREE_REPO}" "${SRC_DIR}"
fi

cd "${SRC_DIR}"

CURRENT="$(git rev-parse HEAD)"
if [ "${CURRENT}" != "${OUT_OF_TREE_COMMIT}" ]; then
   log_info "Checking out the pinned commit ${OUT_OF_TREE_COMMIT} (was ${CURRENT}) ..."
   git fetch --quiet origin "${OUT_OF_TREE_COMMIT}" 2>/dev/null || git fetch --quiet origin
   git checkout --quiet "${OUT_OF_TREE_COMMIT}"
fi

if git apply --reverse --check "${PATCH_FILE}" >/dev/null 2>&1; then
   log_info "The port patch is already applied."
else
   log_info "Applying the port patch for the Linux 7.x kernel API ..."
   git apply "${PATCH_FILE}"
fi

log_info "Source tree ready: ${SRC_DIR}"
log_info "  commit:  $(git rev-parse HEAD)"
log_info "  patch:   ${PATCH_FILE}"
log_info "  changes:"
git --no-pager diff --stat
