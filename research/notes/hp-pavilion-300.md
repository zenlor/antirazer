# HP Pavilion Gaming Mouse 300 (`03f0:2941`) — decoded

OEM rebadge mouse. Same firmware family as the Blackweb gaming mice
(3938:1170 MA32AIY, AYC, AYU); the transport, memory map and record formats
below were cross-checked against the static-analysis decode at
github.com/moxwel/blackweb-mouse-config (PROTOCOL.md) and confirmed live on
the HP hardware. See that document for field-level details this note
summarises.

No public work targets this PID specifically; HP ships no config software.

## USB layout

| interface | class | content |
|---|---|---|
| 0 | mouse | boot mouse: buttons 1–5, X/Y 16-bit, wheel + AC pan |
| 1 | keyboard/consumer/vendor | report 01 keyboard, report 02 consumer, report 04 vendor (`0xFF32`) |

## Transport (interface 1, usage page `0xFF32`, reports are 64 B incl. id)

Output report 4: `[04][cmd][addr lo][addr hi][len][data .. ≤59]`

| cmd | meaning |
|---|---|
| 01 | hello — reply is the status report |
| 02 | begin write session |
| 03 | read len bytes at addr |
| 04 | write data at addr (flash) |
| 05 | write data at addr (settings block) |
| 06 | erase 512-byte flash sector |
| 07 | end session / commit |

Read replies echo cmd/addr/len in bytes 1–4, data from byte 5.

**Status report** (input report 4, `[1]=01`, sent as cmd-01 reply and
spontaneously on onboard changes): `[2]` polling code (1/2/4/8 =
1000/500/250/125 Hz), `[3]` active DPI slot, `[15..]` level table. The HP
variant reports 8 levels u16le at `[20..35]`:

```
1600 2000 2400 2800 3200 3500 4000 5000
```

(preamble `[17..19] = 01 02 c3`; the GET_REPORT control pipe does not answer
for input reports — read them off the interrupt endpoint.)

## Settings block `@0xBE00` (HP variant: 37 bytes + checksum)

```
[0]      polling code (1/2/4/8)
[1]      active DPI slot (0..[5]-1)
[2]      LED mode (bit7 rgb, bits0-6 effect; 03 = fluid)
[3..4]   light speed, brightness
[5]      DPI slot count (04 on this unit)
[6..25]  slots × [enabled][level 0..7][R][G][B]
[26..28] 00 0a 0a (untouched by the vendor app on blackweb too)
[29..36] not decoded (tail on this unit: 00 00 00 14 00 01 00 00)
[37..38] checksum u16le = 0x8032 + Σ bytes[0..0x24]
```

Live unit: slot 0 = 1600 green, slot 1 = 2400 magenta, slot 2 = 3500 white,
slot 3 = 5000 red — the four stages the DPI button cycles, each with its own
colour (the button flashes the new slot's colour, and its choice persists to
flash). LED behaviour on this unit: mode `03` is activity-reactive (lights
on movement/clicks, dark when idle) — not "off". `research/hp-cfg.c` dumps
this block and can write `slot N` / `poll HZ` / `led XX` (apply = cmd 02,
05 @BE00 len 0x27, 07; no sector erase needed).

**Apply** = write + re-enumeration (settings reload at power-on). The vendor
app's live-preview path (feature `0x20`) is **not implemented on this
firmware** — checksum, `00 00` and `32 32` tails all ignored, no status
echo. `hp-cfg ... reset` toggles the USB `authorized` sysfs node for a
replug-free apply; the hello/status reply's slot byte tracks the written
value. `hp-cfg` auto-discovers the FF32 hidraw via /sys (hidraw numbering
moves on replug).

## Button sectors `@0xC000` (8 × 512 B = feature reports `0x10`..`0x17`)

Each sector: primary layer @+0, shift layer @+0xFE, checksum @+0x1FE
(u16le `0x8032 + Σ` over the first 0x1FE bytes). Each layer is
`[type][label][mode][n][n × value,kind]` tokens:

* kind `20` mouse button (1 L, 2 R, 4 M, 8 back, 10 fwd)
* kind `30` device function (04 = DPI cycle)
* kind `00..0F` keys (value = HID usage, low nibble = modifier mask)

Live unit: left/right/middle/back/forward bound to themselves, the two
vendor buttons to device functions `09`/`0a`. The feature reports expose the
first 64 bytes of each sector read-only; full read/write goes through
cmd 03/06/04 at `0xC000 + i·0x200`.

## Tools (all read-only unless stated)

* `research/hp-watch.c` — live diff of the feature records + input tap
* `research/hp-stage.c` — dumps the DPI status frame (kept for reference; its
  write was a wrong guess — byte[1] must be a cmd, not a mirror)
* `research/hp-cfg.c` — settings-block dump, `slot`/`poll` flash writes, and
  `live` (feature-0x20 preview, not persisted)

## Not done yet

Button-sector writes (rebinding — full token grammar in the blackweb doc),
LED/RGB writes (same settings block, fields already decoded), levels/colours
per slot. The WebHID page (`index.html`) could support this PID with the same
transport — reports are 64 bytes with report id, `sendReport`/feature reports
map 1:1.
