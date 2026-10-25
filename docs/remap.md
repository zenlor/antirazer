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

* `device` — `2020`, `2014` or `auto` (default).
* `bindings.normal` — bindings on the normal layer.
* `bindings.hypershift` — 2020 only; the Hypershift overlay.
* `macros` — named, reusable step lists.
* `triggers` — optional macro-trigger overrides, grouped by layer.

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

### Single actions

| value | meaning |
|---|---|
| `x`, `f5`, `enter`, `kp1`, … | key (HID usage names; see `naga-mapper`) |
| `ctrl+shift+t` | key with modifiers (`ctrl shift alt super`, right-hand variants included) |
| `mouse:left\|right\|middle\|back\|forward` | mouse button |
| `hypershift` | layer switch (2020 onboard only) |
| `turbo:f5@3000` | autofire every 3000 ms (2020 onboard, normal layer only) |
| `default` | restore the device default (2020) |

Wheel bindings support keys, mouse buttons and macros. `hypershift`, `turbo`
and `default` are onboard-only.

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

The UI has the wheel up/down controls back as host-side controls. Select one,
choose a key or mouse button and press **Save for daemon**. The **Write
daemon config + reload** button then:

1. reads the current normal and Hypershift records from the mouse,
2. writes them plus the wheel bindings to the daemon config,
3. sends `SIGHUP` to the PID from the PID file.

If the daemon is not running, the config is still written and the status line
says so. DPI keys can be remapped from the same editor; they are written
onboard like the other buttons.

## Requirements and limits

* `apply`/`dump` use the normal libusb path; the udev rule from the README is
  enough.
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

The parser and planner allocate from one arena per map/plan; the JSON parser
is the vendored cJSON. cJSON and cimgui/ImGui are built from `vendor/`.
