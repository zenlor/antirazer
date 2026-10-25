#!/usr/bin/env python3
"""Extract class-02 (mapping domain) transfers from a capture.

usage: baseline_reads.py <capture.pcapng>

Prints the setup field and the first bytes of each class-02 transfer, grouped
by command and data size. Useful on the baseline capture taken while Synapse
connects: it contains the reads and the mapping-table push.

Set NAGA_DEV (device address as in tshark's usb.dst) and TSHARK as needed.
"""
import collections
import os
import subprocess
import sys

TSHARK = os.environ.get("TSHARK", "tshark")
DEV = os.environ.get("NAGA_DEV", "8.16.0")


def control_out(pcap: str) -> list[str]:
    result = subprocess.run(
        [TSHARK, "-r", pcap, "-Y",
         f'usb.dst == "{DEV}" && usb.transfer_type == 0x02',
         "-x"],
        capture_output=True, text=True, check=True)
    chains, buf = [], ""
    for line in result.stdout.splitlines():
        if line.startswith("Packet ("):
            if buf:
                chains.append(buf)
            buf = ""
            continue
        if len(line) >= 8 and line[4:6] == "  ":
            try:
                int(line[:4], 16)
            except ValueError:
                continue
            buf += line[6:53].replace(" ", "")
    if buf:
        chains.append(buf)
    return chains


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__.strip(), file=sys.stderr)
        return 1

    chains = control_out(sys.argv[1])
    print(f"{len(chains)} control-out frames to the device")

    groups = collections.defaultdict(list)
    for chain in chains:
        if len(chain) < 144:  # 64-byte header + at least one data byte
            continue
        payload = bytes.fromhex(chain[128:])
        setup = bytes.fromhex(chain[80:96])  # bytes 40..47 hold the setup
        if len(payload) < 8:
            continue
        cls, cmd, size = payload[6], payload[7], payload[5]
        if cls == 0x02:
            groups[(cmd, size)].append((setup.hex(), payload.hex()))

    for (cmd, size), items in sorted(groups.items()):
        print(f"\nclass 02 cmd {cmd:02X} size {size:02X}: {len(items)} transfers")
        for setup, full in items[:6]:
            print(f"  setup {setup} | {full[16:16 + size * 2]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
