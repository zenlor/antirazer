#!/usr/bin/env python3
"""Reconstruct the device's configuration conversation from a capture.

usage: ceremony.py <capture.pcapng>

Prints control transfers to and from the device in time order, split into
phases at gaps of more than a second, with consecutive identical transfers
collapsed into "xN". This is how Synapse's connect ceremony was decoded:
probe reads, driver mode, DPI, the mapping-table push, commit, then lighting.

Set NAGA_DEV to the device address used by tshark (`usb.src`/`usb.dst`,
"bus.device", e.g. 8.16.0) and TSHARK if it is not on PATH.
"""
import os
import subprocess
import sys

TSHARK = os.environ.get("TSHARK", "tshark")
DEV = os.environ.get("NAGA_DEV", "8.16.0")
FILTER = (f'(usb.src == "{DEV}" || usb.dst == "{DEV}")'
          " && usb.transfer_type == 0x02")


def frame_times(pcap: str) -> list[float]:
    result = subprocess.run(
        [TSHARK, "-r", pcap, "-Y", FILTER, "-T", "fields",
         "-e", "frame.number", "-e", "frame.time_relative"],
        capture_output=True, text=True, check=True)
    times = []
    for line in result.stdout.splitlines():
        fields = line.split("\t")
        if len(fields) >= 2 and fields[1]:
            times.append(float(fields[1]))
    return times


def frame_payloads(pcap: str) -> list[bytes]:
    result = subprocess.run(
        [TSHARK, "-r", pcap, "-Y", FILTER, "-x"],
        capture_output=True, text=True, check=True)
    frames, buf = [], ""
    for line in result.stdout.splitlines():
        if line.startswith("Packet ("):
            if buf:
                frames.append(bytes.fromhex(buf))
            buf = ""
            continue
        if len(line) >= 8 and line[4:6] == "  ":
            try:
                int(line[:4], 16)
            except ValueError:
                continue
            buf += line[6:53].replace(" ", "")
    if buf:
        frames.append(bytes.fromhex(buf))
    return frames


def decode(raw: bytes):
    """usbmon record -> (kind, wIndex, tid, class, cmd, args) or None."""
    if len(raw) < 64:
        return None
    setup = raw[40:48]
    data = raw[64:]
    bm = setup[0]
    widx = setup[4] | setup[5] << 8

    if bm & 0x80:  # IN: request in the submit record, answer in the complete
        if len(data) >= 9 and data[0] in (1, 2, 3):
            return ("RSP", widx, data[1], data[6], data[7],
                    data[8:8 + data[5]])
        return ("GET", widx, 0, 0, 0, b"")
    if len(data) >= 9:
        return ("SET", widx, data[1], data[6], data[7], data[8:8 + data[5]])
    return None


def describe(entry) -> str:
    kind, widx, tid, cls, cmd, args = entry
    if kind == "GET":
        return f"GET   wIndex {widx}"
    if kind == "RSP":
        return (f"RSP   tid {tid:02X} {cls:02X}/{cmd:02X} "
                f"[{len(args)}] {args[:8].hex()}")
    return (f"SET   tid {tid:02X} {cls:02X}/{cmd:02X} "
            f"[{len(args)}] {args[:12].hex()}")


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__.strip(), file=sys.stderr)
        return 1

    times = frame_times(sys.argv[1])
    payloads = frame_payloads(sys.argv[1])
    count = min(len(times), len(payloads))
    entries = []
    for i in range(count):
        decoded = decode(payloads[i])
        if decoded:
            entries.append((times[i], decoded))

    if not entries:
        print("no Razer control transfers found")
        return 1

    phases, current = [], [entries[0]]
    for entry in entries[1:]:
        if entry[0] - current[-1][0] > 1.0:
            phases.append(current)
            current = [entry]
        else:
            current.append(entry)
    phases.append(current)

    t0 = entries[0][0]
    print(f"{len(entries)} transfers in {len(phases)} phases")
    for index, phase in enumerate(phases):
        print(f"\n-- phase {index}: t={phase[0][0] - t0:.1f}"
              f"..{phase[-1][0] - t0:.1f}s ({len(phase)})")
        previous, run = None, 0
        for timestamp, entry in phase:
            if entry == previous:
                run += 1
                continue
            if previous is not None:
                suffix = f"  x{run + 1}" if run else ""
                print(f"  {timestamp - t0:8.2f}  {describe(previous)}{suffix}")
            previous, run = entry, 0
        if previous is not None:
            suffix = f"  x{run + 1}" if run else ""
            print(f"  {phase[-1][0] - t0:8.2f}  {describe(previous)}{suffix}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
