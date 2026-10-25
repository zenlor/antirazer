#!/usr/bin/env bash
# Send one QEMU key combination to the guest through the monitor socket.
#
# usage: ./key.sh meta-r | ./key.sh ret
#
# Needs socat. Set NAGA_VM_DIR if the VM does not use the default state
# directory (see run-windows-vm.sh).
set -euo pipefail

combo="${1:?usage: key.sh <qemu-key-combination>}"
sock="${NAGA_VM_DIR:-$HOME/.local/share/antirazer/vm}/monitor.sock"

[ -S "$sock" ] || { echo "no QEMU monitor at $sock (is the VM running?)" >&2; exit 1; }
command -v socat >/dev/null || { echo "socat not found (nix shell nixpkgs#socat)" >&2; exit 1; }

{ echo "sendkey $combo"; sleep 0.3; } | socat - "unix-connect:$sock" >/dev/null
