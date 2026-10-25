#!/usr/bin/env bash
# Catalog Razer configuration commands in a capture using tshark's full
# dissection (-V), which carries the setup and data fields that -T fields
# cannot always be trusted with.
#
# usage: ./catalog.sh <capture.pcapng>
set -euo pipefail

PCAP="${1:?usage: catalog.sh <capture.pcapng>}"
command -v tshark >/dev/null || { echo "tshark not found" >&2; exit 1; }

tshark -r "$PCAP" -V 2>/dev/null | awk '
/^Frame Number: /  { frame = $3; req = ""; index = ""; hex = "" }
/^    bRequest: /  { req = $3 }
/^    wIndex: /    { index = $2 }
/Data Fragment: /  { hex = $3
	if (req ~ /0x09|SET_REPORT/) {
		status = substr(hex, 1, 2)
		tid    = substr(hex, 3, 2)
		size   = substr(hex, 11, 2)
		class  = substr(hex, 13, 2)
		cmd    = substr(hex, 15, 2)
		crc    = substr(hex, 177, 2)
		args   = substr(hex, 17, 80)
		printf "frame %5s iface %s | status %s tid %s class %s cmd %s size %s crc %s | args %s\n",
		       frame, index, status, tid, class, cmd, size, crc, args
	}
}'
