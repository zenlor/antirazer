#!/usr/bin/env bash
# Capture USB traffic on the Naga's bus (host side, while Synapse/Windows in a
# VM talks to the mouse through usbredir).
#
# usage: sudo ./capture-usb.sh <out.pcapng> [seconds]
#        seconds=0 or omitted captures until Ctrl-C
#
# Needs the usbmon module: sudo modprobe usbmon
set -euo pipefail

OUT="${1:?usage: capture-usb.sh <out.pcapng> [seconds]}"
SECS="${2:-0}"

command -v tshark >/dev/null || { echo "tshark not found" >&2; exit 1; }

BUS=""
for dev in /sys/bus/usb/devices/*-*; do
	[ -r "$dev/idVendor" ] && [ -r "$dev/idProduct" ] || continue
	[ "$(cat "$dev/idVendor")" = "1532" ] || continue
	[ "$(cat "$dev/idProduct")" = "008d" ] || continue
	BUS="$(cat "$dev/busnum")"
	break
done
[ -n "$BUS" ] || { echo "Naga LH 2020 (1532:008d) not found" >&2; exit 1; }

echo "capturing usbmon$BUS -> $OUT"
if [ "$SECS" = "0" ]; then
	exec tshark -i "usbmon$BUS" -w "$OUT"
else
	exec timeout "$SECS" tshark -i "usbmon$BUS" -w "$OUT"
fi
