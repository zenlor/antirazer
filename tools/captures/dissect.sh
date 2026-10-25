#!/usr/bin/env bash
# Summarise USB traffic in a capture: per-endpoint counts, then the control
# transfers with a setup packet (skipping the standard descriptor requests).
#
# usage: ./dissect.sh <capture.pcapng>
set -euo pipefail

PCAP="${1:?usage: dissect.sh <capture.pcapng>}"
command -v tshark >/dev/null || { echo "tshark not found" >&2; exit 1; }

echo "== endpoint traffic counts =="
tshark -r "$PCAP" -T fields -e usb.src -e usb.dst -e usb.endpoint_address \
	-e usb.transfer_type 2>/dev/null |
	sort | uniq -c | sort -rn | head -8

echo
echo "== control transfers with setup/data (host -> device ep0) =="
tshark -r "$PCAP" -Y 'usb.transfer_type == 0x02 &&
	!(usb.setup.bRequest == 6 || usb.setup.bRequest == 9 ||
	  usb.setup.bRequest == 8 || usb.setup.bRequest == 0)' \
	-T fields -e frame.number -e usb.dst -e usb.setup.bmRequestType \
	-e usb.setup.bRequest -e usb.setup.wValue -e usb.setup.wIndex \
	-e usb.setup.wLength 2>/dev/null | head -60
