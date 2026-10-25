#!/usr/bin/env bash
# Screenshot the guest to a PNG file.
#
# usage: ./screenshot.sh [out.png]
#
# Needs socat and either pnmtopng (netpbm) or magick (ImageMagick). Set
# NAGA_VM_DIR if the VM does not use the default state directory (see
# run-windows-vm.sh).
set -euo pipefail

dir="${NAGA_VM_DIR:-$HOME/.local/share/antirazer/vm}"
sock="$dir/monitor.sock"
out="${1:-$dir/screen.png}"
ppm="$dir/screenshot.ppm"

[ -S "$sock" ] || { echo "no QEMU monitor at $sock (is the VM running?)" >&2; exit 1; }
command -v socat >/dev/null || { echo "socat not found (nix shell nixpkgs#socat)" >&2; exit 1; }

rm -f "$ppm"
{ echo "screendump $ppm"; sleep 2; } | socat - "unix-connect:$sock" >/dev/null

if command -v pnmtopng >/dev/null; then
	pnmtopng "$ppm" >"$out"
elif command -v magick >/dev/null; then
	magick "$ppm" "$out"
else
	echo "need pnmtopng (netpbm) or magick (ImageMagick)" >&2
	exit 1
fi
ls -l "$out"
