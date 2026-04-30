#!/usr/bin/env bash
#
# Kernel-bypass tuning for NetClone hosts.
# Apply on every client / coordinator / server node before running any
# of the binaries.  The settings below are kernel-level and persist
# for the remainder of the boot, so this script only needs to run
# once per node per boot.
#
# Usage:
#   sudo ./scripts/host_setup.sh
#
# NOTE: this script does *not* set the per-process locked-memory limit
# (RLIMIT_MEMLOCK).  That is a shell limit, not a sysctl, and must be
# raised in the same shell that launches the client / server binary.
# See scripts/set_memlock_unlimited.sh.

set -euo pipefail

if [[ $EUID -ne 0 ]]; then
  echo "host_setup.sh must run as root (or with sudo)." >&2
  exit 1
fi

sysctl -w net.core.rmem_max=104857600
sysctl -w net.core.rmem_default=104857600

echo 2000000000 > /proc/sys/kernel/shmmax
echo 2048       > /proc/sys/vm/nr_hugepages

echo "[host_setup] sysctl + hugepages applied"
echo "[host_setup] reminder: 'ulimit -l unlimited' is per-shell --"
echo "             see scripts/set_memlock_unlimited.sh"
