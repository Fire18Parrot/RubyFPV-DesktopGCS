#!/usr/bin/env bash
#
# Installs the ground station binaries in a runtime directory that can carry
# the file capabilities, and gives them the capabilities they need.
#
# Why a runtime directory:
#
#   The ground station has to change the settings of its radios: it puts them
#   in monitor mode, it tunes them, it sets their power, and it captures the
#   frames of the air with libpcap. Those operations need the CAP_NET_ADMIN
#   and the CAP_NET_RAW capabilities, and the application is never run as
#   root.
#
#   A file capability is granted to a file, and the kernel ignores the
#   capabilities of a file that is on a filesystem mounted with the "nosuid"
#   option. The binaries of this project are built on a data disk that is
#   mounted with "nosuid", so a capability set on them would be silently
#   ignored at execution time. The binaries are therefore copied to a
#   runtime directory on a filesystem that is not mounted with "nosuid", and
#   the capabilities are set on the copies.
#
#   The ground station is started from that runtime directory (run_gcs.sh),
#   so the resources it loads (the "res" folder) are found through a
#   symbolic link to the ones of the project.
#
# What the capabilities are:
#
#   CAP_NET_ADMIN   to change the mode, the channel and the power of a radio
#                   interface.
#   CAP_NET_RAW     to open the packet socket that the capture uses.
#
#   The interface that carries the network connection of the machine is never
#   reconfigured: the application refuses to touch it (see
#   hardware_radio_is_protected_interface() in code/base/hardware_radio.c).
#   The capability is not a reason to break the network of the machine.
#
# The script needs the root privileges, because a file capability can only be
# set by a process that has the CAP_SETFCAP capability.
#
# On this desktop the password is asked by the polkit agent of the desktop,
# so the script is run with pkexec and not with sudo:
#
#    pkexec code/radio/install_gcs_capabilities.sh
#
# (sudo asks the password on a terminal, that is not available here.)
#
# It has to be run again after every rebuild: the build writes new files, and
# the capability belongs to the file, not to the path.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"

RUN_DIR="${XDG_DATA_HOME:-${HOME}/.local/share}/rubyfpv/run"

# The script runs with the root privileges, but the ground station is run by
# the user, with the configuration of the user. The runtime directory is
# therefore in the home of the user that asked for the installation, and not
# in the home of root: the user has to be able to execute the binaries and
# to read them.
TARGET_UID="${SUDO_UID:-${PKEXEC_UID:-}}"
TARGET_HOME="${HOME}"
if [ -n "${TARGET_UID}" ] && [ "${TARGET_UID}" != "0" ]; then
   TARGET_HOME="$(getent passwd "${TARGET_UID}" | cut -d: -f6)"
fi
if [ -z "${TARGET_HOME}" ]; then
   echo "ERROR: the home directory of the user that asked for the installation was not found." >&2
   exit 1
fi
RUN_DIR="${TARGET_HOME}/.local/share/rubyfpv/run"

# The binaries of the ground station, and the capabilities they get.
# The logger and the player do not configure the radios: they do not get any
# capability.
BINARIES_WITH_CAPS="ruby_start ruby_controller ruby_central ruby_rt_station"
BINARIES_WITHOUT_CAPS="ruby_logger ruby_player_desktop"

CAPS="cap_net_admin,cap_net_raw"

if [ "$(id -u)" != "0" ]; then
   echo "ERROR: this script needs the root privileges: pkexec $0" >&2
   exit 1
fi

if [ ! -d "${REPO_DIR}/res" ]; then
   echo "ERROR: the resources folder ${REPO_DIR}/res is missing." >&2
   exit 1
fi

echo "The runtime directory is ${RUN_DIR}."

mkdir -p "${RUN_DIR}"

for b in ${BINARIES_WITH_CAPS} ${BINARIES_WITHOUT_CAPS}; do
   if [ ! -f "${REPO_DIR}/${b}" ]; then
      echo "ERROR: the binary ${REPO_DIR}/${b} is missing. Build the ground station first." >&2
      exit 1
   fi
   # The copy replaces the previous one: the capability belongs to the file,
   # and a new file has no capability.
   cp -f "${REPO_DIR}/${b}" "${RUN_DIR}/${b}"
   chmod 755 "${RUN_DIR}/${b}"
   # A file that was copied from a file with a capability keeps the
   # capability only if it is copied with the "xattr" preservation, and the
   # capability is set again below in any case.
   setcap -r "${RUN_DIR}/${b}" 2>/dev/null || true
done

# The user has to be able to run the ground station and to replace the
# binaries after a rebuild.
#
# The owner is changed before the capabilities are set: changing the owner of
# a file clears its capabilities, the kernel drops the "security.capability"
# attribute when the owner changes.
if [ "${TARGET_UID}" != "0" ] && [ -n "${TARGET_UID}" ]; then
   chown -R "${TARGET_UID}" "${RUN_DIR}"
fi

for b in ${BINARIES_WITH_CAPS}; do
   setcap "${CAPS}+epi" "${RUN_DIR}/${b}"
   echo "  $(getcap "${RUN_DIR}/${b}")"
done

for h in iw ip; do
   if ! command -v "$h" >/dev/null 2>&1; then
      echo "WARNING: system helper $h is missing"
      continue
   fi
   cp -f "$(command -v "$h")" "${RUN_DIR}/${h}"
   chmod 755 "${RUN_DIR}/${h}"
   setcap -r "${RUN_DIR}/${h}" 2>/dev/null || true
   setcap "${CAPS}+epi" "${RUN_DIR}/${h}"
   echo "  $(getcap "${RUN_DIR}/${h}")"
done

# The resources of the ground station are loaded with a path that is relative
# to the current working directory, and the ground station runs from the
# runtime directory: they are reached through a symbolic link.
ln -sfn "${REPO_DIR}/res" "${RUN_DIR}/res"
mkdir -p "${RUN_DIR}/tmp"

echo ""
echo "The capabilities are installed in ${RUN_DIR}."
echo "Start the ground station with: ${SCRIPT_DIR}/run_gcs.sh"
echo "After every rebuild, run this script again (the capability belongs to the file)."
