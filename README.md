# antirazer

Configuration tool for the **Razer Naga Left-Handed Edition 2020**
(USB `1532:008d`) on Linux.

It remaps the twelve thumb buttons — and reads the rest of the mouse — straight
from the device firmware. No Razer Synapse, no Windows VM, no replug: changes
are written to the mouse's flash and applied immediately.

Two frontends share one device core:

| | |
|---|---|
| `naga-mapper` | scriptable command-line tool |
| `naga-ui` | small SDL3 + cimgui window with a clickable drawing of the mouse |

## Status

| feature | state |
|---|---|
| thumb-button remapping (all 12) | verified live on hardware |
| modifiers (ctrl / shift / alt / super) | verified live (standard HID bitmap) |
| apply without replug | verified (`00/B3` commit command) |
| turbo / autofire | implemented — assignment type `0D`, rate = `1000000 / delay_ms` |
| DPI set | implemented (`04/05`), not verified on hardware yet |
| hypershift layer | record field is written and read; end-to-end behaviour not verified |
| main buttons, wheel, DPI keys | readable and listed; writes use the thumb-button input class and are unverified |
| profiles, macros, lighting | not decoded — macros are host-side by design (use evdev/uinput) |

Everything below that touches the device is the mapping domain (`02/0C` write,
`02/8C` read, `00/B3` commit); no other command is ever sent.

## Install

### Nix

```sh
nix build                # -> result/bin/naga-mapper, result/bin/naga-ui
nix profile install .    # or: nix profile install git+https://git.sr.ht/~zenlor/antirazer
```

### From source

```sh
nix develop -c make      # toolchain, libusb, SDL3 and cimgui from the flake
sudo make install        # PREFIX=/usr/local by default
```

Outside Nix you need `libusb-1.0`, `SDL3` (for the GUI) and `cimgui` built as a
static library; pass its prefix with `make CIMGUI=/path/to/cimgui`. The exact
cimgui revision is pinned in `nix/cimgui.nix`.

### Device permissions

```sh
sudo install -m644 udev/99-naga.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
```

The rule uses `TAG+="uaccess"` (logind grants the active session user access).
Without systemd, replace it with `MODE="0660", GROUP="users"`. After installing
the rule, replug the mouse once.

## CLI

```sh
naga-mapper list                     # every control and its current binding
naga-mapper get 40                   # read one record
naga-mapper set 40 x                 # thumb button 1 -> "x"
naga-mapper set 41 ctrl+shift+t      # thumb button 2 -> Ctrl+Shift+t
naga-mapper set 42 f5 01             # button 3 -> F5 on the hypershift layer
naga-mapper turbo 40 f5 3000         # button 1 -> F5 every 3 s while held
naga-mapper commit                   # re-apply the stored table
naga-mapper dpi 1600 1600            # set DPI (counts per inch)
```

* Thumb buttons: `40`..`4B` = `1`..`9`, `0`, `-`, `=` as labelled on the device
* Main buttons: `01`..`03` = left / right / middle
* Wheel: `04` / `05`; DPI keys: `0B` / `0C`
* Keys: `a`..`z`, `0`..`9`, `f1`..`f24`, `enter`, `esc`, `space`, `tab`, `pgup`,
  `kp1`, `playpause`, ... or a raw HID usage in hex (`1a`, `0x1a`)
* Modifiers: `ctrl`, `shift`, `alt`, `super`, `rctrl`, ... joined with `+`
* Layers: `00` normal (default), `01` hypershift

Settings live in the mouse's flash and survive reboots and moving the mouse to
another machine.

## GUI

```sh
naga-ui
```

The left column shows the thumb panel and a top view of the mouse; every
control is clickable and its current binding is drawn on it (hover for a
tooltip). The right column edits the selected control: key, modifiers, layer,
turbo with delay, then **Write + commit**. DPI sits below the drawing. The
status line reports each device operation.

`naga-ui --screenshot shot.bmp` renders the window, saves a screenshot and
exits — handy for smoke tests on headless machines.

## How it works

The mouse exposes a vendor feature report on HID interface 0. Each exchange is
a 90-byte payload:

```
[0]    status (request 00; response 01 busy, 02 ok, 03 fail)
[1]    transaction id (0x1F on this generation)
[2..3] remaining packets (0, big endian)
[4]    protocol type (0)
[5]    data size
[6]    command class
[7]    command id
[8..]  arguments
[88]   CRC = XOR of bytes 2..87
[89]   0
```

Mapping records are ten argument bytes `01 <ctr> <layer> <class> <type>
<mod> <usage> 00 00`: the control instance (`ctr`), the layer, the assignment
type (`00` default, `02` keyboard key, `0D` turbo), and the HID modifier map
plus keyboard usage.

| operation | command | arguments |
|---|---|---|
| write record | `02/0C` | `01 ctr layer 02 type mod usage ...` |
| read record | `02/8C` | `01 ctr 00 02` |
| commit | `00/B3` | `00` |
| set DPI | `04/05` | `00 x_lo x_hi y_lo y_hi` |

Two quirks are worth knowing:

* This generation only answers transaction id `0x1F`. Other tools send `0xFF`
  to interface 0, which makes the firmware reset its USB link for a few
  seconds. antirazer always uses `0x1F`.
* A write is only applied after `00/B3`; without it the new record waits for
  the next enumeration (replug).

The full reference — every decoded command, the record layout, what is still
unknown and why the warnings exist — is in [`docs/protocol.md`](docs/protocol.md).

## Protocol research

The protocol was recovered from Synapse 3 USB captures of this exact device
(`1532:008d`), then verified from Linux. The documents and the rig are part of
the repository:

| document | content |
|---|---|
| [`docs/protocol.md`](docs/protocol.md) | decoded transport, commands, records, quirks and open questions |
| [`docs/research-log.md`](docs/research-log.md) | how it was decoded: findings, dead ends, and the one incident that damaged a record |
| [`docs/vm-capture.md`](docs/vm-capture.md) | reproducing Synapse ground truth in a Windows VM |

[`tools/`](tools/) holds the utilities used for that work: a raw protocol probe
(`naga-probe`), an evdev event tap, usbmon capture and analysis scripts, and
the QEMU VM harness. None of it is needed to configure the mouse.

Raw material — USB captures, VM disks, vendor installers and firmware — is not
committed: it is large, machine-specific and partly vendor data. The documents
describe how to recreate and analyse it instead.

The status table above is the honest boundary of what has been verified on
hardware; sibling devices of the same generation probably share the encoding,
but nothing here is tested against them.

## Troubleshooting

**"Naga LH 2020 not found (check the udev rule or permissions)"** — install the
udev rule above and replug, or run the tool as root.

**"could not claim the HID interface"** — another process owns the mouse, for
example a VM with USB passthrough or a gaming daemon. Release it there first.

**The mouse disappears for ~5 seconds** — something sent a non-`0x1F`
transaction id to interface 0; the firmware re-enumerates. antirazer does not
do this, but older Razer tools do.

**I want my old layout back** — writes are on the device. Rebind the thumb
buttons with `naga-mapper set <ctr> <key>` (the factory bindings are `1`..`9`,
`0`, `-`, `=` on `40`..`4B`).

## Hacking

```
src/naga.c        USB transport, request encoding, mapping/DPI writes
src/naga_keys.c   HID usage <-> name, modifier parsing, binding rendering
src/naga_ctrls.c  the control table (names, ctr values, groups)
src/mapper/       naga-mapper CLI
src/ui/           naga-ui: ui_state.c (logic), ui_layout.c (ImGui), main.c (SDL3)
tests/            unit tests for the pure key/control code
tools/            research: naga-probe, evtap, captures/ analysis, vm/ harness
docs/             protocol reference, research log, VM capture guide
nix/              cimgui derivation, package, dev shell
udev/             device permission rule
```

```sh
nix develop          # toolchain + dependencies
make                 # naga-mapper and naga-ui
make test            # key parsing / naming / control-table tests
make tools           # research utilities: naga-probe, evtap
make install         # PREFIX=... UDEVDIR=...
```

`make CIMGUI=<prefix>` builds the GUI outside Nix. C sources carry
`SPDX-License-Identifier: GPL-2.0-or-later OR BSD-2-Clause` headers.

## License

Dual-licensed under `GPL-2.0-or-later` or `BSD-2-Clause`, at your option. See
`LICENSE` and `COPYING`.
