#!/usr/bin/env bash
#
# Static ARP setup for the data-plane network.
# Run on every host that participates in NetClone experiments
# (clients, coordinator, and servers).
#
# The switch does not handle host network setup, so each host needs a
# full ARP table for the data-plane interface before any client /
# server binary can talk to its peers.  This works on any link speed
# the testbed actually runs at -- our reference numbers are from a
# 100 GbE deployment, but the script does not depend on it.
#
# Edit the PEERS array below so that it lists every (data-plane IP,
# data-plane NIC MAC) pair in your testbed.  The entries shown are
# kept as a working example from our cluster -- replace them with
# your own.
#
# Usage:
#   sudo ./scripts/setup_arp.sh
#   arp -an    # verify

set -euo pipefail

if [[ $EUID -ne 0 ]]; then
  echo "setup_arp.sh must run as root (or with sudo)." >&2
  exit 1
fi

# Format: "<data-plane IP>  <data-plane NIC MAC>"
PEERS=(
  "10.0.1.101  0c:42:a1:2f:12:e6"
  "10.0.1.102  0c:42:a1:2f:11:c6"
  "10.0.1.103  10:70:fd:1c:d4:b8"
  "10.0.1.104  10:70:fd:0d:d5:4c"
  # ... add one line per peer on the data network
)

for entry in "${PEERS[@]}"; do
  read -r ip mac <<<"$entry"
  arp -s "$ip" "$mac"
done

echo "[setup_arp] installed ${#PEERS[@]} ARP entries"
