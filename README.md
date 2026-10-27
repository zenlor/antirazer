# antirazer

Configuration tool for the **Razer Naga Left-Handed Edition 2020**
(USB `1532:008d`) on Linux.

It remaps the twelve thumb buttons — and reads the rest of the mouse — straight
from the device firmware. No Razer Synapse, no Windows VM, no replug: changes
are written to the mouse's flash and applied immediately.

The mapping tools share one device core:

| | |
|---|---|
| `naga-mapper` | scriptable command-line tool |
| `naga-ui` | small GLFW + OpenGL3 + cimgui window with a clickable drawing, host-side wheel controls and daemon config export |
| `naga-remap` | JSON maps, onboard apply, host-side macros and a reloadable daemon |

## Status

| feature | state |
|---|---|
| thumb-button remapping (all 12) | verified live on hardware |
| modifiers (ctrl / shift / alt / super) | verified live (standard HID bitmap) |
| apply without replug | verified (`00/B3` commit command) |
| turbo / autofire | implemented — assignment type `0D`, rate = `1000000 / delay_ms` |
| mouse-button actions | decoded from captures (type `01`); implemented, writes not yet exercised on hardware |
| DPI set | implemented (`04/05`), not verified on hardware yet |
| hypershift layer | switch action decoded (type `0B`, inferred) and implemented; hold-vs-toggle behaviour unverified |
| main buttons, DPI keys | default actions decoded; DPI keys are onboard-remappable through the same record path |
| wheel up/down | host-side via the daemon (`REL_WHEEL` interception); not an onboard control |
| file-driven remapping | `naga-remap`: JSON map, onboard apply for the 2020, host-side run for both |
| sequences / macros | host-side via `naga-remap run`; the firmware stores single actions only |
| daemon | `naga-remap daemon`; config in `$XDG_CONFIG_HOME/naga-remap/config.json`, SIGHUP reload |
| profiles, lighting | not decoded |

Everything below that touches the device is the mapping domain (`02/0C` write,
`02/8C` read, `00/B3` commit); no other command is ever sent.

## Install

### Nix

```sh
nix build                # -> result/bin/naga-mapper, naga-ui, naga-remap
nix profile install .    # or: nix profile install git+https://git.sr.ht/~zenlor/antirazer
```

### From source

```sh
nix develop -c make      # toolchain, libusb, libevdev, GLFW/OpenGL; cJSON/cimgui vendored
sudo make install        # PREFIX=/usr/local by default
```

Outside Nix you need `libusb-1.0`, `libevdev` and `GLFW` with OpenGL support. cJSON and
cimgui/ImGui are vendored under `vendor/` and built from source, so there is
no extra prefix to point at.

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
naga-mapper set 43 mouse:middle      # button 4 -> middle click
naga-mapper set 44 hypershift        # button 5 -> Hypershift layer switch
naga-mapper turbo 40 f5 3000         # button 1 -> F5 every 3 s while held
naga-mapper commit                   # re-apply the stored table
naga-mapper dpi 1600 1600            # set DPI (counts per inch)
```

* Thumb buttons: `40`..`4B` = `1`..`9`, `0`, `-`, `=` as labelled on the device
* Main buttons: `01`..`03` = left / right / middle
* Wheel click: `03`; DPI keys: `0B` / `0C`. Scroll up/down is a HID wheel
  axis, not an assignable control
* Keys: `a`..`z`, `0`..`9`, `f1`..`f24`, `enter`, `esc`, `space`, `tab`, `pgup`,
  `kp1`, `playpause`, ... or a raw HID usage in hex (`1a`, `0x1a`)
* Mouse: `mouse:left`, `mouse:right`, `mouse:middle`, `mouse:back`,
  `mouse:forward`
* Layer switch: `hypershift`
* Modifiers: `ctrl`, `shift`, `alt`, `super`, `rctrl`, ... joined with `+`
* Layers: `00` normal (default), `01` hypershift. Keys take modifiers before
  the layer; mouse actions and `hypershift` take the layer directly
  (`set 40 mouse:back 01`).

Settings live in the mouse's flash and survive reboots and moving the mouse to
another machine.

## GUI

```sh
naga-ui
```

The left column shows the thumb panel and a top view of the mouse; every
control is clickable and its current binding is drawn on it (hover for a
tooltip). Wheel up/down are shown as host-side controls and are saved in the
daemon config rather than the mouse's flash. The right column edits the
selected control: its binding (keyboard key, mouse button or Hypershift
switch), modifiers, layer and turbo delay, then **Stage change**.

Edits are queued per control: staged controls get a yellow border and dot,
and selecting another control keeps the queued edit. **Apply all (N)** writes
all staged onboard changes in one block and refreshes the daemon config when
wheel bindings changed. **Discard all** clears the queue; **Re-read** drops
the selected staged edit. Pressing Enter in the raw usage field also stages
the edit. DPI applies immediately, and the **Write daemon config + reload**
button can still be used explicitly. The status line reports each operation.

The binding field has three ways in:

* a searchable chooser that starts with **mouse & layer actions** — mouse
  left/right/middle/back/forward and Hypershift — followed by keys labelled
  with the current **keyboard layout** (the labels follow the system keymap,
  but the value stored is always the HID usage, so a binding keeps working on
  a different layout),
* a **Record** button that captures the next key press (with its modifiers) or
  mouse button; `Esc` cancels. While recording, the button shows
  “Recording…” because a click is the binding,
* the raw usage field for keyboard keys not in the list (`f5`, `enter`, `1a`).

When a mouse or Hypershift action is selected, the key/usage, modifier and
turbo fields are disabled; the layer selector still applies.

`naga-ui --screenshot shot.bmp` renders the window, saves a screenshot and
exits — handy for smoke tests on headless machines.

The GUI uses GLFW + OpenGL3, which supports both Wayland and X11. GLFW selects
the session's backend automatically (Wayland when available) and sets the
Wayland app id; no X11 wrapper is needed. The ImGui OpenGL3 backend is wired
to GLFW for its function pointers.

On NixOS, native Wayland also needs access to `/dev/dri/card*` (usually the
`video` group) and a working EGL driver. If your user is not in that group,
GLFW reports `EGL: Failed to get EGL display`; add the user
(`sudo usermod -aG video "$USER"`), log out and back in, and retry. X11/XWayland
remains usable without that access.

## From the browser

`index.html` is the same configuration protocol as a single static page over
WebHID — no build, no server side. Serve it locally and open it in a
Chromium-family browser (Firefox and Safari do not ship WebHID):

```sh
python3 -m http.server 8000        # then http://localhost:8000
```

It is also served ready-to-use at <https://zenlor.github.io/antirazer/>.

Click **Connect**, pick the Naga entry that answers (the mouse exposes several
interfaces; the page probes each selection and keeps the one that talks the
mapping protocol), and the tool reads every control on both layers. Edits are
written to flash and committed immediately, DPI included. The udev rule above
must be installed, and naga-ui / the naga-remap daemon must be closed — the
config channel is exclusive.

## Remapping from a file

`naga-remap` applies a JSON map and runs host-side bindings:

```sh
naga-remap validate examples/naga-2020.json
naga-remap apply examples/naga-2020.json     # 2020: onboard single actions
naga-remap run examples/naga-2020.json       # apply + run host-side bindings
naga-remap run examples/naga-2014.json --model 2014
naga-remap daemon                            # reloads on SIGHUP
```

The file has `bindings.normal`, an optional `bindings.hypershift`, named
`macros`, and optional `triggers`. See [docs/remap.md](docs/remap.md) for ids,
actions, macros, wheel bindings and trigger assignment. `run`/`daemon` need
root (or access to `/dev/uinput` and the event nodes); Ctrl-C restores the
grabbed devices.

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

Mapping records are ten argument bytes `01 <ctr> <layer> <type> <length>
<data...>`: the control instance (`ctr`), the layer, the action type (`01`
mouse button, `02` keyboard key, `0B` Hypershift, `0D` autofire, …), the data
length and its payload. A keyboard payload is a HID modifier map plus usage; a
mouse payload is a button id.

| operation | command | arguments |
|---|---|---|
| write record | `02/0C` | `01 ctr layer type length data...` |
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

## OpenRazer

[`contrib/openrazer/`](contrib/openrazer/) contains a patch that brings the
same onboard button actions to OpenRazer — a `button_action` sysfs file in the
`razermouse` kernel module and `razer.device.onboard` DBus methods in the
daemon — plus a NixOS overlay exported as `overlays.default`:

```nix
{ nixpkgs.overlays = [ antirazer.overlays.default ]; }
```

See [`contrib/openrazer/README.md`](contrib/openrazer/README.md) for the API,
patch files and verification status.

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
src/remap/        naga-remap: JSON map, planner, config/PID, libevdev/uinput
                  runner and daemon
src/ui/           naga-ui: ui_state.c (logic), ui_layout.c (ImGui),
                  ui_keys.c (layout-aware key names and capture), main.c (GLFW/OpenGL3)
tests/            unit tests for the key/control and remap code
examples/         example naga-remap JSON maps
tools/            research: naga-probe, evtap, captures/ analysis, vm/ harness
docs/             protocol reference, research log, VM capture guide
nix/              package and dev shell
vendor/           cJSON and cimgui/ImGui (MIT), built from source
udev/             device permission rule
```

```sh
nix develop          # toolchain + dependencies
make                 # naga-mapper, naga-remap and naga-ui
make test            # key/control and remap tests
make tools           # research utilities: naga-probe, evtap
make install         # PREFIX=... UDEVDIR=...
```

C sources carry `SPDX-License-Identifier: GPL-2.0-or-later OR BSD-2-Clause`
headers. The vendored cJSON and cimgui/ImGui sources keep their MIT licenses
in `vendor/`.

## License

Dual-licensed under `GPL-2.0-or-later` or `BSD-2-Clause`, at your option. See
`LICENSE` and `COPYING`.
