# Remapping from a file

`naga-remap` applies a JSON map to a Naga. Single actions can live in the
2020 LH's onboard mapping; macros and wheel rotation always run host-side,
and the 2014 has no onboard mapping at all.

## Quick start

```sh
nix develop -c make

./naga-remap validate examples/naga-2020.json
./naga-remap apply examples/naga-2020.json      # 2020 LH, writes flash
./naga-remap run examples/naga-2020.json        # applies + runs host-side
./naga-remap run examples/naga-2014.json --model 2014
./naga-remap daemon                             # stay resident, SIGHUP reloads
```

`run`/`daemon` need root (or access to the event nodes **and** `/dev/uinput`).
Ctrl-C restores the grabbed devices.

## Commands

| command | device | what it does |
|---|---|---|
| `validate FILE` | — | parse the map and check ids/actions/macros |
| `apply FILE [--dry-run]` | 2020 | write single actions and macro trigger keys onboard |
| `run FILE [--device PATH] [--model 2020\|2014]` | both | grab the devices and run host-side bindings |
| `daemon [--config FILE] [--device PATH] [--model 2020\|2014]` | both | stay resident; SIGHUP reloads the map |
| `dump [--output FILE]` | 2020 | export the onboard map as JSON |
| `devices` | — | list Naga event nodes |

For the 2020, `run`/`daemon` first apply the map (single actions and macro
triggers), then grab the keyboard interface for macro triggers and the mouse
interface for the wheel. For the 2014 everything is host-side; nothing is
written to the mouse.

Use `--device /dev/input/eventN` for the keyboard/side-button node when
several Naga nodes are present. `--model` is required only when `device` is
`auto` and more than one mouse is connected.

## The map

```json
{
  "device": "2020",
  "bindings": {
    "normal": {
      "1": "x",
      "2": "ctrl+shift+t",
      "3": "mouse:middle",
      "4": "hypershift",
      "5": "turbo:f5@3000",
      "6": "macro:hello",
      "wheel-up": "pgup",
      "wheel-down": "pgdn"
    },
    "hypershift": {
      "1": "f13"
    }
  },
  "macros": {
    "hello": [
      {"key": "a"},
      {"delay": 50},
      {"text": "hello"},
      {"mouse": "left"}
    ]
  },
  "triggers": {
    "normal": { "6": "f20" }
  }
}
```

* `version` — optional; `1` is the current envelope version.
* `device` — `2020`, `2014`, `hp-300`, or `auto` (default).
* `match` — optional `{vendorId, productId}` hint used by the web page and a
  future generic driver; the CLI may ignore it.
* `bindings.normal` — bindings on the normal layer.
* `bindings.hypershift` — the second layer (2020 Hypershift, HP shift layer).
* `macros` — named, reusable step lists.
* `triggers` — optional macro-trigger overrides, grouped by layer.
* `settings` — optional, device-specific settings (DPI, polling, slots).
* `backup` — optional raw device snapshot (hex). `naga-remap` accepts and
  ignores it; the web page uses it for lossless restore.

The web configurator and `naga-remap` share this envelope. Unknown top-level
keys are ignored for forward compatibility; a driver only reads the sections
it implements, so a Naga file may carry `settings`/`backup` and an HP file may
carry `macros` without breaking either side.

A binding is always a string. Sequences live under `macros` and are referenced
with `"macro:<name>"`, so one macro can be bound to several buttons/layers.

### Ids

2020 LH (`bindings.normal` and `bindings.hypershift`):

* `1`..`9`, `0`, `-`, `=` — thumb grid, as printed on the mouse
* `left`, `right`, `middle` — main buttons (wheel click is `middle`)
* `dpi-up`, `dpi-down` — DPI keys (onboard)
* `wheel-up`, `wheel-down` — scroll directions, host-side only (normal layer)
* a raw ctr in hex (`40` … `4b`, `01` … `03`, `0b`, `0c`)

2014 (`bindings.normal` only): the thumb labels `1`..`9`, `0`, `-`, `=` plus
the host-side `wheel-up` / `wheel-down`.

`dpi-up`/`dpi-down` are normal onboard controls: Synapse can remap them, and
so can this tool — they are written through the same `02/0C` record path as
the thumb buttons. Wheel rotation is not an assignable onboard control: the
firmware exposes it as a relative HID axis, so those two ids are handled by
the userspace daemon.

HP Pavilion 300 (`bindings.normal` and `bindings.hypershift`):

* `sector-0` … `sector-7` — the eight flash button sectors.
* sector 3 is physical button 1 (top left); sector 5 is physical button 4 /
  the DPI button (bottom right). Sectors 0–2 are assumed to be the main
  buttons; 4, 6 and 7 are not mapped yet, so treat their ids as flash
  addresses rather than physical labels.
* the HP shift layer is exposed as `hypershift`; the web inspector keeps it
  read-only until its activation is understood.

A raw ctr in hex (`40` … `4b`, `01` … `03`, `0b`, `0c`) also works for the
2020, and the web page uses hex ids (`09`, `0a`, `0e`, `34`, `35`) for the
controls it reads but does not draw.

### Single actions

| value | meaning |
|---|---|
| `x`, `f5`, `enter`, `kp1`, … | key (HID usage names; see `naga-mapper`) |
| `ctrl+shift+t` | key with modifiers (`ctrl shift alt super`, right-hand variants included) |
| `mouse:left\|right\|middle\|back\|forward` | mouse button |
| `hypershift` | layer switch (2020 onboard only) |
| `turbo:f5@3000` | autofire every 3000 ms (2020 onboard, normal layer only) |
| `dpi:up` / `dpi:down` | onboard DPI action (2020) |
| `dpi:cycle` | onboard DPI cycle action (HP sector 5) |
| `device:<hex>` | HP device function (for example `device:0a`) |
| `default` | restore the device default (2020; an empty HP layer) |

Wheel bindings support keys, mouse buttons and macros. `hypershift`, `turbo`
and `default` are onboard-only.

The browser accepts every action above, and so does `naga-remap`: `apply`
writes `dpi:*`/`device:*` actions onboard, and `dump` emits the same spelling
back. HP host-side `run` is the remaining gap (see the support table below).

### Settings

`settings` is optional and driver-owned. Unknown keys inside it are ignored.

Naga:

```json
"settings": { "dpi": { "x": 1600, "y": 2400 } }
```

HP Pavilion 300:

```json
"settings": {
  "polling": 1000,
  "activeSlot": 1,
  "slotCount": 4,
  "slots": [
    { "enabled": true, "level": 0, "color": "#00ff00" },
    { "enabled": true, "level": 2, "color": "#ff00ff" }
  ]
}
```

`polling` is in Hz (1000/500/250/125); `level` indexes the level table the
mouse reports; `color` is the per-slot RGB value the DPI button flashes.
`slotCount` is informational. HP settings apply at the next replug.

### Macros

A macro is a list of steps. Every step has exactly one action:

```json
{"key": "ctrl+c", "hold_ms": 20}
{"text": "hello", "hold_ms": 10, "delay_ms": 20}
{"mouse": "left", "action": "click", "hold_ms": 20}
{"delay": 100}
```

* `key` — a key combo; modifiers are pressed around the key. Default hold 20 ms.
* `text` — typed one character at a time with `hold_ms` (default 10) and
  `delay_ms` between characters (default 20). US layout, ASCII only.
* `mouse` — `click` (default), `down` or `up`; default hold 20 ms.
* `delay` — milliseconds.

Macros are emitted through a uinput copy of the mouse's interfaces. Unrelated
keys and events pass through unchanged.

### Triggers

The 2020 has no firmware macro support, so a macro needs a trigger key that
the daemon intercepts. By default macros get `f13`, `f14`, … (up to `f24`) in
binding order. Override per layer:

```json
"triggers": { "normal": { "6": "f20" }, "hypershift": { "1": "f21" } }
```

On the 2014 the side buttons already emit keys. Defaults are `KEY_KP1` …
`KEY_KP9`, `KEY_KP0`, `KEY_KPMINUS`, `KEY_KPPLUS`; override them when your unit
emits something else:

```json
"triggers": { "normal": { "1": "KEY_1", "2": "KEY_2" } }
```

Use `naga-remap devices` to find the event node, and `evtap` to see what the
buttons actually emit. On the 2020, `triggers` is only valid for macro
bindings; on the 2014 it applies to every non-wheel binding.

## The daemon

```sh
naga-remap daemon [--config FILE] [--device PATH] [--model 2020|2014]
```

Without `--config`, the daemon reads
`$XDG_CONFIG_HOME/naga-remap/config.json` (fallback
`~/.config/naga-remap/config.json`). It writes its PID to
`$XDG_RUNTIME_DIR/naga-remap.pid` (fallback `/tmp/naga-remap-<uid>.pid`).

On start it loads and applies the map, then grabs the devices. `SIGHUP`
reloads the file from disk, re-applies the onboard part and rebuilds the
host-side plans without losing the PID. `SIGINT`/`SIGTERM` restore the
devices and remove the PID file.

The 2020's relayed wheel directions are suppressed while the daemon is
running when a `wheel-up`/`wheel-down` binding exists; an unmapped direction
passes through normally. Movement, buttons and horizontal tilt are always
forwarded through the uinput copy.

## Exporting from the GUI

### naga-ui

The UI has the wheel up/down controls back as host-side controls. Select a
control, edit its binding and press **Stage change**; the edit is queued (the
control gets a yellow marker) and you can move on to the next one. **Apply
all (N)** then writes every staged onboard change in one block. If any wheel
binding was staged, it also:

1. reads the current normal and Hypershift records from the mouse,
2. writes them plus the wheel bindings to the daemon config,
3. sends `SIGHUP` to the PID from the PID file.

If the daemon is not running, the config is still written and the status line
says so. **Discard all** clears the queue, and **Re-read** drops the selected
staged edit. The **Write daemon config + reload** button remains available
for an explicit export. DPI keys can be remapped from the same editor; they
are written onboard like the other buttons.

### Web configurator

`site/index.html` uses the same automatic staging model for both mice. Every
edit (binding, DPI, HP polling/slot) is queued the moment it is made and
stored in `localStorage`; the sketch marks queued controls in amber, and each
queue row can be reverted individually.

* **Flush changes** writes the queued onboard entries in one block and reads
  the device back. Host-side wheel entries are not written.
* **Download JSON** exports the envelope above from the last read plus the
  queued edits: `bindings`, device `settings`, and a raw `backup` section.
* **Import JSON** stages a map of the same shape instead of writing it.
* **Save backup** stores a raw snapshot; the inspector can restore a single
  control or sector from it.
* **Reset queue** discards the queued edits; **profiles** save/load named
  configurations, and loading a profile queues its differences.

The HP page is gone; `/hp.html` redirects to the unified page. Append
`?selftest=1` to show the protocol/parser/queue checks.

### CLI support today

| device | `validate` | `apply` | `run` |
|---|---|---|---|
| 2020 LH | yes | yes | yes |
| 2014 | yes | no (no onboard map) | yes |
| HP Pavilion 300 | yes | yes (settings + sectors) | no (host-side next phase) |

The JSON envelope is shared; the web page and `naga-remap` read and write the
same `hp-300` bindings, `settings` and `backup` sections. HP `run`/`daemon`
still stop with a clear error — onboard `apply` is the supported path.

## Requirements and limits

* `apply`/`dump` for the 2020 use libusb; hp-300 uses the FF32 hidraw node.
  The udev rule from the README is enough for the 2020; the HP needs read/write
  access to its hidraw device (close other configurators first).
* `run`/`daemon` grab evdev nodes and create uinput devices: root (or
  `uinput` + event-node access) is required.
* Macro triggers are swallowed, so don't also use F13–F24 on the 2020 for
  ordinary keyboard input. Onboard key/turbo actions must not emit a trigger
  key either; the tool rejects such a map.
* `hypershift`, `turbo` and `default` are onboard-only and cannot be run
  host-side.
* `dump` reads normal and Hypershift records; layer-1 read-back uses the same
  request layout as layer 0.
* Profiles, lighting and firmware macros are out of scope. The 2014 has no
  onboard remapping; `run` is the only option.
* HP sector 5 is also the onboard DPI button: pressing it re-applies factory
  bindings until the next replug, even after a successful custom write.
* HP host-side remapping in `run`/`daemon` is not implemented yet; use
  `apply` to write the onboard actions and settings. The sector↔physical
  mapping is still partial, so host-side ids are not stable enough to grab.

The parser and planner allocate from one arena per map/plan; the JSON parser
is the vendored cJSON. cJSON and cimgui/ImGui are built from `vendor/`.
