# Sourced helper that lifts RLIMIT_MEMLOCK in the *current* shell.
#
# IMPORTANT: ulimit is a shell builtin, so it only affects the shell
# that executed it (and processes spawned from that shell).  Running
# this file as
#
#     bash scripts/set_memlock_unlimited.sh
#
# changes the limit only for the subshell that bash spins up to run
# the script -- your interactive shell, and any binaries you launch
# from it afterwards, will still hit the default 64 KiB cap and VMA
# will fail with "cannot allocate memory" on huge-page registration.
#
# Source it instead, in the same shell that will run the client /
# server binary:
#
#     source scripts/set_memlock_unlimited.sh
#     # or, equivalently,
#     . scripts/set_memlock_unlimited.sh
#
# For a permanent cluster-wide fix (recommended for long-running
# testbeds), add the following line to /etc/security/limits.conf and
# log out / log in once:
#
#     *    -    memlock    unlimited

ulimit -l unlimited
echo "[memlock] RLIMIT_MEMLOCK in this shell: $(ulimit -l)"
