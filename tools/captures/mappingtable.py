#!/usr/bin/env python3
"""Extract the mapping-table push (class 02, command 0C, size 0A) from a capture.

usage: mappingtable.py <capture.pcapng>

Synapse sends one record per control and layer on connect. Each line shows the
record's ten argument bytes plus the control instance and layer decoded from
them. That push is what revealed the record format.

Set NAGA_DEV to the device address used by tshark (e.g. 8.16.0) and TSHARK if
it is not on PATH.
"""
import os
import subprocess
import sys

TSHARK = os.environ.get("TSHARK", "tshark")
DEV = os.environ.get("NAGA_DEV", "8.16.0")


def payloads(pcap: str) -> list[bytes]:
    result = subprocess.run(
        [TSHARK, "-r", pcap, "-Y",
         f'usb.dst == "{DEV}" && usb.transfer_type == 0x02 && frame.len == 154',
         "-x"],
        capture_output=True, text=True, check=True)
    chains, buf = [], ""
    for line in result.stdout.splitlines():
        if line.startswith("Packet ("):
            if len(buf) >= 300:
                chains.append(buf)
            buf = ""
            continue
        if len(line) >= 8 and line[4:6] == "  ":
            try:
                int(line[:4], 16)
            except ValueError:
                continue
            buf += line[6:53].replace(" ", "")
    if len(buf) >= 300:
        chains.append(buf)
    return [bytes.fromhex(c[128:308]) for c in chains if len(c) >= 308]


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__.strip(), file=sys.stderr)
        return 1

    seen = 0
    for payload in payloads(sys.argv[1]):
        if payload[6] != 0x02 or payload[7] != 0x0C or payload[5] != 0x0A:
            continue
        args = payload[8:18]
        seen += 1
        print(f"{seen:3d}: ctr {args[1]:02X} layer {args[2]:02X} | "
              f"{' '.join(f'{b:02X}' for b in args)}")
    print(f"{seen} mapping records")
    return 0


if __name__ == "__main__":
    sys.exit(main())
