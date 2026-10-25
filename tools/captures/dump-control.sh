#!/usr/bin/env bash
# Dump all control-transfer payloads between host and one USB device.
#
# usage: ./dump-control.sh <capture.pcapng> <device, e.g. 8.16.0>
set -euo pipefail

PCAP="${1:?usage: dump-control.sh <capture.pcapng> <bus.device>}"
DEV="${2:?usage: dump-control.sh <capture.pcapng> <bus.device>}"
command -v tshark >/dev/null || { echo "tshark not found" >&2; exit 1; }

tshark -r "$PCAP" -Y "usb.dst == \"$DEV\"" \
	-T fields -e frame.number -e usb.setup.bmRequestType -e usb.setup.bRequest \
	-e usb.setup.wValue -e usb.setup.wIndex -e usb.setup.wLength \
	-e usb.capdata 2>/dev/null |
	grep -vE '^[0-9]+[[:space:]]*$'
