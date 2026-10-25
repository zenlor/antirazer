#!/usr/bin/env bash
# Dump the unique interrupt-IN HID payloads from one endpoint, with counts.
#
# usage: ./in-ep.sh <capture.pcapng> <source, e.g. 8.16.2>
set -euo pipefail

PCAP="${1:?usage: in-ep.sh <capture.pcapng> <bus.device.interface>}"
SRC="${2:?usage: in-ep.sh <capture.pcapng> <bus.device.interface>}"
command -v tshark >/dev/null || { echo "tshark not found" >&2; exit 1; }

tshark -r "$PCAP" -Y "usb.src == \"$SRC\"" -T fields -e usbhid.data 2>/dev/null |
	grep -v '^$' | sort | uniq -c | sort -rn
