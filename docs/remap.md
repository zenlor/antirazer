# Remapping from a file

`naga-remap` applies a JSON map to a Naga. Single actions can live in the
2020 LH's onboard mapping; macros (sequences) always run host-side, and the
2014 has no onboard mapping at all.

## Quick start

```sh
nix develop -c make

./naga-remap validate examples/naga-2020.json
./naga-remap apply examples/naga-2020.json      # 2020 LH, writes flash
./naga-remap run examples/naga-2020.json        # applies + runs macros
./naga-remap run examples/naga-2014.json --model 2014
```

`run` needs root (or access to the event node **and** `/dev/uinput`). Ctrl-C
restores the grabbed device.

## Commands

| command | device | what it does |
|---|---|---|
| `validate FILE` | — | parse the map and check ids/actions/macros |
| `apply FILE [--dry-run]` | 2020 | write single actions and macro trigger keys onboard |
| `run FILE [--device PATH] [--model 2020\|2014]` | both | grab the device and play macros (2014: all bindings) |
| `dump [--output FILE]` | 2020 | export the onboard map as JSON |
| `devices` | — | list Naga event nodes |

For the 2020, `run` first applies the map (single actions and macro triggers),
then grabs the keyboard interface and intercepts the macro triggers. For the
2014 it only runs host-side; nothing is written to the mouse.

Use `--device /dev/input/eventN` when several Naga nodes are present.
`--model` is required only when `device` is `auto` and more than one mouse is
connected.

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
      "6": "macro:hello"
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
* `triggers` — optional host-side trigger overrides, grouped by layer.

A binding is always a string. Sequences live under `macros` and are referenced
with `"macro:<name>"`, so one macro can be bound to several buttons/layers.

### Ids

2020 LH (`bindings.normal` and `bindings.hypershift`):

* `1`..`9`, `0`, `-`, `=` — thumb grid, as printed on the mouse
* `left`, `right`, `middle` — main buttons (wheel click is `middle`)
* `dpi-up`, `dpi-down` — DPI keys
* a raw ctr in hex (`40` … `4b`, `01` … `03`, `0b`, `0c`)

2014 (`bindings.normal` only): the thumb labels `1`..`9`, `0`, `-`, `=`.

### Single actions

| value | meaning |
|---|---|
| `x`, `f5`, `enter`, `kp1`, … | key (HID usage names; see `naga-mapper`) |
| `ctrl+shift+t` | key with modifiers (`ctrl shift alt super`, right-hand variants included) |
| `mouse:left\|right\|middle\|back\|forward` | mouse button |
| `hypershift` | layer switch (2020 onboard only) |
| `turbo:f5@3000` | autofire every 3000 ms (2020 onboard, normal layer only) |
| `default` | restore the device default (2020) |

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

Macros are emitted through a uinput copy of the mouse's keyboard interface.
Unrelated keys and events pass through unchanged.

### Triggers

The 2020 has no firmware macro support, so a macro needs a trigger key that
`run` can intercept. By default macros get `f13`, `f14`, … (up to `f24`) in
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
bindings; on the 2014 it applies to every binding.

## Requirements and limits

* `apply`/`dump` use the normal libusb path; the udev rule from the README is
  enough.
* `run` grabs an evdev node and creates a uinput device: root (or `uinput` +
  event-node access) is required.
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
