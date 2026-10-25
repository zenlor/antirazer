#!/usr/bin/env python3
"""Decode and diff onboard mapping records from USB captures.

usage:
  records.py <capture.pcapng> [more.pcapng ...]
  records.py --diff <before.pcapng> <after.pcapng>
  records.py --self-test

A mapping record is the ten-byte payload of a class-02 / command-0C write:

  <store> <ctr> <layer> <type> <length> <data...>

The decoder knows the types observed so far (keyboard, mouse button, autofire,
macros) and prints anything else as raw bytes, which is exactly what a new
capture of Synapse needs to reveal.

In --diff mode the last write per (store, ctr, layer) is compared between two
captures, so a capture made after a reconnect (Synapse pushes its whole table)
shows every binding that differs from the earlier one.

The Naga's address is detected from its USB ID; set NAGA_DEV to override it.
Set TSHARK if the binary is not on PATH.
"""

import os
import subprocess
import sys

TSHARK = os.environ.get("TSHARK", "tshark")
DEV = os.environ.get("NAGA_DEV")  # None = detect from the capture

TYPE_NAMES = {
    0x00: "default",
    0x01: "mouse",
    0x02: "key",
    0x03: "macro",
    0x04: "macro-repeat",
    0x06: "device-function",
    0x07: "device-function",
    0x0D: "autofire",
    0x0E: "wheel-tilt",
}

MOUSE_BUTTONS = {
    1: "left",
    2: "right",
    3: "middle",
    4: "back",
    5: "forward",
}

MODIFIERS = ((0x01, "ctrl"), (0x02, "shift"), (0x04, "alt"), (0x08, "super"))

# Canonical names for the usages that are awkward to derive. Everything else
# falls back to a hex usage, which is enough for a protocol capture.
NAMED_USAGES = {
    0x28: "enter", 0x29: "esc", 0x2A: "backspace", 0x2B: "tab",
    0x2C: "space", 0x2D: "minus", 0x2E: "equal", 0x2F: "lbracket",
    0x30: "rbracket", 0x31: "bslash", 0x32: "hash", 0x33: "semicolon",
    0x34: "quote", 0x35: "grave", 0x36: "comma", 0x37: "dot",
    0x38: "slash", 0x39: "caps", 0x46: "print", 0x47: "scroll",
    0x48: "pause", 0x49: "insert", 0x4A: "home", 0x4B: "pgup",
    0x4C: "delete", 0x4D: "end", 0x4E: "pgdn", 0x4F: "right",
    0x50: "left", 0x51: "down", 0x52: "up", 0x53: "numlock",
    0x65: "application", 0x66: "power", 0x7F: "mute", 0x80: "volup",
    0x81: "voldown", 0xB5: "next", 0xB6: "prev", 0xB7: "stop",
    0xB8: "eject", 0xCD: "playpause",
}


def usage_name(usage):
    if 0x04 <= usage <= 0x1D:
        return chr(ord("a") + usage - 0x04)
    if 0x1E <= usage <= 0x26:
        return chr(ord("1") + usage - 0x1E)
    if usage == 0x27:
        return "0"
    if 0x3A <= usage <= 0x45:
        return f"f{usage - 0x3A + 1}"
    if 0x68 <= usage <= 0x73:
        return f"f{usage - 0x68 + 13}"
    return NAMED_USAGES.get(usage, f"0x{usage:02x}")


def decode(record):
    """Human-readable action from a ten-byte mapping record."""
    if len(record) < 5:
        return "short record"

    store, ctr, layer, typ, length = record[:5]
    params = record[5:5 + length]
    where = f"ctr {ctr:02X} layer {layer:02X}"
    raw = record.hex(" ")

    if typ == 0x00:
        action = "default"
    elif typ == 0x01 and length >= 1:
        name = MOUSE_BUTTONS.get(params[0], f"button {params[0]}")
        action = f"mouse {name}"
    elif typ == 0x02 and length >= 2:
        mod = params[0]
        mods = "+".join(name for bit, name in MODIFIERS if mod & bit)
        action = f"key {mods + '+' if mods else ''}{usage_name(params[1])}"
    elif typ == 0x0D and length >= 4:
        mod = params[0]
        mods = "+".join(name for bit, name in MODIFIERS if mod & bit)
        rate = (params[2] << 8) | params[3]
        delay = 1000000 // rate if rate else 0
        action = (f"autofire {mods + '+' if mods else ''}{usage_name(params[1])}"
                  f" every {delay} ms")
    elif typ in (0x03, 0x04) and length >= 2:
        macro = (params[0] << 8) | params[1]
        action = f"{TYPE_NAMES[typ]} id {macro}"
    else:
        action = f"{TYPE_NAMES.get(typ, f'type {typ:02x}')} data {params.hex(' ')}"

    return f"{where} | {action} | {raw}"


def hexdump_chains(stdout):
    """Split `tshark -x` output into raw hex chains."""
    chains, buf = [], ""
    for line in stdout.splitlines():
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
    return chains


def device_address(pcap):
    """The device's usbmon address (bus.device), detected from its USB ID."""
    if DEV:
        return DEV

    result = subprocess.run(
        [TSHARK, "-r", pcap, "-Y",
         "usb.idVendor == 0x1532 && usb.idProduct == 0x008d",
         "-T", "fields", "-e", "usb.src", "-e", "usb.dst"],
        capture_output=True, text=True)
    addresses = []
    for line in result.stdout.splitlines():
        for address in line.split("\t"):
            address = address.strip()
            if address and address != "host" and "." in address \
                    and address not in addresses:
                addresses.append(address)
    if not addresses:
        raise SystemExit(
            "could not find the Naga (1532:008d) in the capture; "
            "set NAGA_DEV to the bus.device address (e.g. 8.16.0)")
    return addresses[0]


def mapping_records(pcap):
    """All class-02/command-0C records written to the device, in order."""
    dev = device_address(pcap)
    result = subprocess.run(
        [TSHARK, "-r", pcap, "-Y",
         f'usb.dst == "{dev}" && usb.transfer_type == 0x02 && frame.len == 154',
         "-x"],
        capture_output=True, text=True, check=True)

    records = []
    for chain in hexdump_chains(result.stdout):
        payload = bytes.fromhex(chain[128:308])  # 64-byte header, 90 data
        if len(payload) < 18:
            continue
        if payload[6] == 0x02 and payload[7] == 0x0C and payload[5] == 0x0A:
            records.append(payload[8:18])
    return records


def latest_table(records):
    """Last write per (store, ctr, layer), which is the effective binding."""
    table = {}
    for record in records:
        table[(record[0], record[1], record[2])] = record
    return table


def diff_records(base_records, after_records):
    base = latest_table(base_records)
    after = latest_table(after_records)
    changed = []
    for key in sorted(set(base) | set(after)):
        if base.get(key) != after.get(key):
            changed.append((key, base.get(key), after.get(key)))
    return base, after, changed


def show_capture(path):
    records = mapping_records(path)
    print(f"== {path}: {len(records)} mapping writes, "
          f"{len(latest_table(records))} distinct bindings")
    for index, record in enumerate(records, 1):
        print(f"{index:3d}: {decode(record)}")
    print()
    return records


def show_diff(before, after):
    base_records = mapping_records(before)
    after_records = mapping_records(after)
    base, after, changed = diff_records(base_records, after_records)

    print(f"before {before}: {len(base_records)} writes, {len(base)} bindings")
    print(f"after  {after}: {len(after_records)} writes, {len(after)} bindings")
    print(f"\n{len(changed)} changed binding(s):")
    for (store, ctr, layer), old, new in changed:
        print(f"\nstore {store:02X} ctr {ctr:02X} layer {layer:02X}:")
        print(f"  before: {decode(old) if old else '(not pushed)'}")
        print(f"  after:  {decode(new) if new else '(not pushed)'}")
    return len(changed)


def self_test():
    key = bytes([1, 0x40, 0x00, 0x02, 0x02, 0x03, 0x17, 0, 0, 0])
    mouse = bytes([1, 0x41, 0x00, 0x01, 0x01, 0x03, 0, 0, 0, 0])
    auto = bytes([1, 0x42, 0x00, 0x0D, 0x04, 0x00, 0x3E, 0x01, 0x4D, 0])
    layer2 = bytes([1, 0x43, 0x01, 0x02, 0x02, 0x00, 0x68, 0, 0, 0])

    assert "key ctrl+shift+t" in decode(key), decode(key)
    assert "mouse middle" in decode(mouse), decode(mouse)
    assert "autofire f5 every 3003 ms" in decode(auto), decode(auto)
    assert "ctr 43 layer 01" in decode(layer2), decode(layer2)
    assert "default" in decode(bytes([1, 0x44, 0, 0, 0, 0, 0, 0, 0, 0]))

    base, after, changed = diff_records([key, auto], [key, auto, mouse])
    assert len(changed) == 1, changed
    (_, ctr, layer), old, new = changed[0]
    assert ctr == 0x41 and layer == 0 and old is None and new == mouse

    # A rewritten record must show up as changed even if it was in both captures
    changed_key = bytes([1, 0x40, 0x00, 0x02, 0x02, 0x00, 0x04, 0, 0, 0])
    _, _, changed = diff_records([key], [changed_key])
    assert len(changed) == 1 and changed[0][1] == key
    print("records.py self-test passed")


def main(argv):
    if len(argv) == 2 and argv[1] == "--self-test":
        self_test()
        return 0
    if len(argv) == 4 and argv[1] == "--diff":
        show_diff(argv[2], argv[3])
        return 0
    if len(argv) < 2:
        print(__doc__.strip(), file=sys.stderr)
        return 1
    for pcap in argv[1:]:
        show_capture(pcap)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
