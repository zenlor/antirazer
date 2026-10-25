#!/usr/bin/env python3
"""Catalog the Razer SET_REPORT writes in a USB capture.

usage: analyze.py <capture.pcapng> [more.pcapng ...]

Extracts the 90-byte vendor feature-report payloads written to the device,
groups them by transaction id / class / command / size and prints every
distinct argument set with a count — mapping writes stand out immediately.
Lighting frames (class 0F, command 03) are counted but not listed.

Requires tshark with USB support. Set NAGA_DEV to the device address as it
appears in the capture (tshark's `usb.dst`, "bus.device", e.g. 8.16.0); set
TSHARK if the binary is not on PATH. Reading usbmon captures usually needs
root or the appropriate capabilities.
"""
import collections
import os
import subprocess
import sys

TSHARK = os.environ.get("TSHARK", "tshark")
DEV = os.environ.get("NAGA_DEV", "8.16.0")


def hexdump_payloads(pcap: str) -> list[bytes]:
    """90-byte payloads of SET_REPORT transfers to the device, in order."""
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
    # 64-byte usbmon header, then the 90-byte Razer frame
    return [bytes.fromhex(c[128:308]) for c in chains if len(c) >= 308]


def is_lighting(payload: bytes) -> bool:
    # extended matrix custom frame: class 0F, command 03, one row at a time
    return payload[6] == 0x0F and payload[7] == 0x03


def catalog(name: str, payloads: list[bytes]) -> None:
    groups = collections.defaultdict(collections.Counter)
    lighting = 0
    for payload in payloads:
        if is_lighting(payload):
            lighting += 1
            continue
        key = (payload[1], payload[6], payload[7], payload[5])
        groups[key][payload[8:8 + payload[5]].hex()] += 1

    print(f"=== {name}: {len(payloads)} writes, {lighting} lighting frames")
    for (tid, cls, cmd, size), counts in sorted(groups.items()):
        print(f"  tid {tid:02X} class {cls:02X} cmd {cmd:02X} size {size:02X}: "
              f"{sum(counts.values())} writes, {len(counts)} distinct")
        for args, n in counts.most_common():
            print(f"      x{n}: {args}")
    print()


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__.strip(), file=sys.stderr)
        return 1
    for pcap in sys.argv[1:]:
        catalog(pcap, hexdump_payloads(pcap))
    return 0


if __name__ == "__main__":
    sys.exit(main())
