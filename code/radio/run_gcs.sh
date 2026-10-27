#!/usr/bin/env bash
#
# Starts the ground station from the runtime directory, where the binaries
# carry the capabilities that are needed to configure the radios and to
# capture the frames of the air.
#
# The script does not need any privilege: the capabilities are on the files,
# they are given to the process by the kernel when it executes them.
#
# The runtime directory is created and updated by
# code/radio/install_gcs_capabilities.sh, that needs the root privileges and
# that has to be run again after every rebuild.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
RUN_DIR="${XDG_DATA_HOME:-${HOME}/.local/share}/rubyfpv/run"

if [ ! -x "${RUN_DIR}/ruby_start" ]; then
   echo "ERROR: the runtime directory ${RUN_DIR} does not have the ground station." >&2
   echo "       Run: sudo ${SCRIPT_DIR}/install_gcs_capabilities.sh" >&2
   exit 1
fi

# The binaries of the project are the ones that are built: if they are newer
# than the copies, the capabilities are missing (the build writes new files)
# and the ground station would start without them.
for b in ruby_start ruby_controller ruby_central ruby_rt_station; do
   if [ "${REPO_DIR}/${b}" -nt "${RUN_DIR}/${b}" ]; then
      echo "WARNING: ${b} was rebuilt after the capabilities were installed." >&2
      echo "         Run: sudo ${SCRIPT_DIR}/install_gcs_capabilities.sh" >&2
   fi
done

# The capabilities are the difference between a ground station that can
# configure its radios and capture the frames of the air, and one that can
# only draw its menus. The check is made here, because this is the moment
# where it is useful to know.
if ! getcap "${RUN_DIR}/ruby_rt_station" 2>/dev/null | grep -q "cap_net_admin"; then
   echo "WARNING: the binaries of ${RUN_DIR} have no CAP_NET_ADMIN / CAP_NET_RAW capability." >&2
   echo "         The ground station will NOT be able to tune its radios nor to" >&2
   echo "         capture the frames of the air (the pairing search cannot work)." >&2
   echo "         Run: pkexec ${SCRIPT_DIR}/install_gcs_capabilities.sh" >&2
fi

# Fix for sudo: pass through session environment for the renderer
if [ "$(id -u)" -eq 0 ]; then
   # When running via sudo, the original user's environment is often lost.
   # We try to recover XDG_RUNTIME_DIR and WAYLAND_DISPLAY from the calling user.
   # SUDO_USER is set by sudo to the username of the user who ran the command.
   if [ -n "${SUDO_USER:-}" ]; then
      USER_ID=$(id -u "${SUDO_USER}")
      export XDG_RUNTIME_DIR="/run/user/${USER_ID}"

      # Try to find WAYLAND_DISPLAY by looking at the user's environment if possible,
      # otherwise default to wayland-0.
      export WAYLAND_DISPLAY="wayland-0"
      if [ -f "/proc/self/environ" ]; then
         # This is a guess, as /proc/self/environ is for the current process (root)
         # but some sudo configs preserve some vars.
         V_DISP=$(strings /proc/self/environ | grep WAYLAND_DISPLAY | cut -d= -f2)
         [ -n "$V_DISP" ] && export WAYLAND_DISPLAY="$V_DISP"
      fi
   fi
fi

# Detach the dedicated radio interfaces of the ground station from the
# network manager of the machine. A network manager that keeps managing a
# monitor mode interface runs background scan cycles on it, and every scan
# cycle can retune the interface on another frequency or change its type
# (monitor -> managed). While the interface is drifted like this, the radio
# links are completely dead: no frame is sent and no frame is received on
# the expected frequency, until the network manager drifts the interface
# back (self-recovery) or the ground station is restarted. These monitor
# interfaces are not network connections of the machine: they must never be
# managed.
if command -v iw >/dev/null 2>&1; then
   for mondev in $(iw dev 2>/dev/null | awk '/Interface/{i=$2} /type monitor/{print i}'); do
      if command -v nmcli >/dev/null 2>&1; then
         nmcli device set "$mondev" managed no 2>/dev/null || true
      fi
   done
fi

cd "${RUN_DIR}" || exit 1
# The runtime directory contains capability-carrying copies of iw and ip,
# named iw and ip and ahead of their system counterparts, so that the
# helper commands resolve to files with cap_net_admin and cap_net_raw.
export PATH="${RUN_DIR}:${PATH}"
exec ./ruby_start "$@"
