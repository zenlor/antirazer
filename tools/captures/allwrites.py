#!/usr/bin/env python3
"""List every control-OUT transfer to the device, in capture order.

usage: allwrites.py <capture.pcapng>

Unlike analyze.py this does not assume the 90-byte layout: it prints the setup
packet and raw data of all control transfers, which is what you want when
looking for a command family with a different framing.

Set NAGA_DEV (device address as in tshark's usb.dst) and TSHARK as needed.
"""
import os
import subprocess
import sys

TSHARK = os.environ.get("TSHARK", "tshark")
DEV = os.environ.get("NAGA_DEV", "8.16.0")


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__.strip(), file=sys.stderr)
        return 1

    result = subprocess.run(
        [TSHARK, "-r", sys.argv[1], "-Y",
         f'usb.dst == "{DEV}" && usb.transfer_type == 0x02 && frame.len > 64',
         "-T", "fields",
         "-e", "frame.number", "-e", "usb.setup.bmRequestType",
         "-e", "usb.setup.bRequest", "-e", "usb.setup.wValue",
         "-e", "usb.setup.wIndex", "-e", "usb.setup.wLength",
         "-e", "usb.capdata"],
        capture_output=True, text=True, check=True)

    for line in result.stdout.splitlines():
        fields = line.split("\t")
        if len(fields) < 7 or not fields[6]:
            continue
        frame, bm, req, value, index, length, data = fields
        # -T fields repeats the 8-byte setup packet in front of the data
        words = data.split()
        if len(words) >= 8 and words[0] == "21" and words[1] == "09":
            words = words[8:]
        print(f"frame {frame:>6} bm {bm} req {req} wValue {value} "
              f"wIndex {index} len {length:>3} | {' '.join(words)[:96]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
