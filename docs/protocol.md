# Protocol reference — Razer Naga Left-Handed Edition 2020 (`1532:008d`)

This is the decoded configuration protocol as far as it is known today. It was
recovered by capturing Razer Synapse 3 talking to this exact device under
Windows (see [`vm-capture.md`](vm-capture.md)) and then verified against the
mouse from Linux. The status column says how far that verification goes — do
not read "observed" as "understood".

antirazer only ever sends the mapping commands (class `02`) and DPI (class
`04`) described below.

## Transport

The device exposes a 90-byte vendor **feature report** on HID interface 0:

| operation | request type | request | value | index |
|---|---|---|---|---|
| SET_REPORT | `0x21` | `0x09` | `0x0300` | 0 |
| GET_REPORT | `0xA1` | `0x01` | `0x0300` | 0 |

Payload layout:

```
[0]     status    request 00; response 01 busy, 02 ok, 03 rejected
[1]     tid       transaction id, 0x1F on this generation
[2..3]  remaining packets, big endian, 0
[4]     protocol type, 0
[5]     data size (number of argument bytes)
[6]     command class
[7]     command id
[8..]   arguments
[88]    CRC = XOR of bytes 2..87
[89]    0
```

A transaction is SET_REPORT, a short settle delay (50 ms is enough), then
GET_REPORT to read the 90-byte answer. The answer repeats the request framing;
the interesting part is the status byte and the arguments.

**Transaction id.** This generation only answers to `0x1F`. Tools that send
`0xFF` (the id used by older Razer devices and by OpenRazer's generic code)
make the firmware reset its USB link: the mouse disappears for roughly five
seconds and comes back. Always use `0x1F`.

**CRC.** Computed over bytes 2..87 inclusive and stored at byte 88. The
firmware rejects frames with a wrong CRC.

### Interfaces

| interface | role | notes |
|---|---|---|
| 0 | mouse + 90-byte vendor feature report | this is the configuration channel |
| 1 | keyboard | a SET_REPORT here disconnects the device immediately |
| 2 | consumer control | 22-byte asynchronous events (button/stage reports) on its IN endpoint |
| 3 | LampArray (usage page `0x59`) | own 63-byte feature report and lighting protocol |

Interface 0 is the only one that carries the mapping protocol. Sending an
unrelated payload there can wedge the firmware (see *Gotchas*).

## Command domains

Classes observed on this device, with Synapse's transaction id (`0x1F`) unless
noted:

| class | ids seen | domain | status |
|---|---|---|---|
| `00` | `04` `05` `81` `82` `84` `85` `B3` | device info, mode, commit | partially decoded |
| `02` | `0C` `8C` `82` `84` | **button mapping** | decoded, implemented |
| `04` | `05` `06` `80` `83` `85` `86` | DPI and stage table | partially decoded, set is implemented |
| `05` | `80` `81` `8A` (×12) | profiles | observed only |
| `06` | `80` `8B` `8C` `8E` | sensor / surface calibration | observed only |
| `0B` | `03` `0B` | power (?) | observed only |
| `0F` | `02` `03` `80` `82` | lighting | observed only |
| `14` | `00` `10` `20` | newer-generation writes (tid `0x20`) | not used by this device |

## Button mapping (class `02`)

### Read

`02/8C`, size `0x0A`, arguments `01 <ctr> 00 <class>`:

```
01   record marker
ctr  control instance (table below)
00   layer requested (00 normal)
02   input class (thumb buttons)
```

The answer carries the record in its arguments. The leading `01` is a record
marker that Synapse increments as a rolling counter; any non-zero value works.
Reads are flaky directly after a burst of writes — the device echoes the
request with status `00` instead of answering; wait about a second and retry.

### Write

`02/0C`, size `0x0A`, arguments:

```
01 <ctr> <layer> <class> <type> <mod> <usage> 00 00 00
```

* `class` — `02` for thumb buttons (the only value verified for writes)
* `type` — `02` keyboard key, `0D` autofire, `00` device default
* `mod` — standard HID modifier bitmap: bit0 LCtrl, bit1 LShift, bit2 LAlt,
  bit3 LGUI, bits 4..7 the right-hand equivalents
* `usage` — HID keyboard usage (`0x04`=`a` … `0x1D`=`z`, `0x1E`..`0x27`=`1`..`0`,
  `0x28` Enter, `0x2C` Space, `0x3A`..`0x45` F1..F12, `0x68`..`0x73` F13..F24 …)

The device accepts the write with status `02` and stores it in flash, but the
new binding only becomes active after the commit command.

Synapse's capture groups the same ten bytes as
`01 <ctr> <layer> <input-id×3> <assign×4>`, where the input id for a thumb
button is `02 02 00` and a keyboard assignment is `02 <mod> <usage> 00`. Both
descriptions produce the same bytes for a keyboard binding; the record layout
beyond that is one of the open questions below.

### Commit

`00/B3`, size `0x01`, argument `00`. Re-applies the stored table; without it a
write waits for the next enumeration (replug). Mappings live in flash and
survive reboots and moving the mouse between machines.

### Control instances

| ctr | control | notes |
|---|---|---|
| `01` `02` `03` | left, right, middle | main buttons |
| `04` `05` | wheel up, wheel down | |
| `0B` `0C` | DPI up, DPI down | |
| `0E` | profile cycle (?) | seen in captures |
| `40`..`48` | thumb buttons `1`..`9` | write verified live |
| `49` | thumb button `0` | write verified live |
| `4A` | thumb button `-` | write verified live |
| `4B` | thumb button `=` | write verified live |

The thumb grid is three columns by four rows; the printed labels run
`1 2 3 / 4 5 6 / 7 8 9 / 0 - =`. Writing to the other controls uses the same
command shape but a different input class in the firmware's model, and has not
been verified — see the status table in the README.

### Autofire (turbo)

Written with the same `02/0C` command, size `0x0A`:

```
01 <ctr> 00 0D 04 <mod> <usage> <rate_hi> <rate_lo> 00
```

`rate = 1000000 / delay_ms`, big endian (a 3000 ms delay is `0x015E`). The
deliberately weird `0D 04` pair is what Synapse sends; the rate field is not
present in a normal key record.

Reading an autofire record back is **not verified**. The mapping read decoder
in antirazer treats the type byte as `0x0D`, which matches the write position
of the marker but not necessarily what the device echoes; the UI therefore
keeps the editor fields you entered after a successful write instead of
re-deriving them from the read-back.

### Assignment types

The firmware's mapping engine knows 165 assignment types
(`MAP_MACRO`, `MAP_MACROKEY`, `MAP_HYPERSHIFTKEY`, `MAP_KEYMAP_UP`/`_DN`,
`MAP_JS_OBM_PROFILE_*`, `MAP_DPI_*`, `MAP_LEDCOLOUR`, `MAP_LEDEFFECT`,
`MAP_LAUNCH_PROGRAM`, `MAP_OFF`, `MAP_TEXT_BLOCK`, media keys …). Those names
come from static analysis of the Synapse mapping module; only the numeric
values for keyboard (`02`) and autofire (`0D`) have been confirmed against the
device, and `00` is used for a pristine/default record.

### Layers

Each record carries a layer byte: `00` normal, `01` hypershift. Synapse pushes
both layers for every control on connect. Writing layer `01` stores a
hypershift binding, but the end-to-end behaviour of hypershift (what toggles
it, how it interacts with the profile key) has not been verified.

## DPI (class `04`)

| command | size | arguments | meaning |
|---|---|---|---|
| `04/05` | `07` | `00 <x_lo> <x_hi> <y_lo> <y_hi> 00 00` | set current DPI (little endian) |
| `04/06` | `26` | `01 03 05 00 0190 0190 0000 0103 …` | five-stage DPI table |
| `04/85` | `02` | `1400…` | current DPI read-back |
| `04/83`, `04/86` | | | stage table reads |

`0190` = 400, `0320` = 800, `0640` = 1600, `0C80` = 3200, `1900` = 6400.
antirazer implements `04/05` plus the commit; the stage table is not exposed
yet.

## Device information (class `00`)

| command | size | meaning |
|---|---|---|
| `00/81` | `02` | firmware version |
| `00/82` | `16` | serial number (22 characters) |
| `00/84` | `02` | device mode: `00` normal, `03` driver mode |
| `00/04` | `02` | set device mode (`0300` on, `0000` off) |
| `00/85` | `02` | polling rate |
| `00/B3` | `01` | commit / apply configuration |

## Profiles, sensor and lighting

These were seen in captures but are not decoded; they are listed so that
future work does not have to rediscover the entry points.

* **Profiles:** `05/80` (1 byte), `05/81` (65 bytes) and `05/8A` (1 byte)
  per profile, twelve of each in the connect capture; responses carry a
  `05 01` tag.
* **Surface calibration:** `06/80`, `06/8B`, `06/8C` (70/64 bytes, paged
  table reads from `0x77BA` and `0x63CA`) and `06/8E` (drift / lift-off
  distance query).
* **Lighting:** `0F/80` (three LED groups of 25), `0F/82` (matrix dimensions),
  `0F/02`, and `0F/03` extended custom frames per LED (size `0x0E`, one row at
  a time; the side panel is six rows of 21 columns on interface 2).

## Synapse's connect ceremony

Roughly, in order:

1. probe loop: `0F/80`, `0F/82`, `0F/02`, `00/81`, `00/82`, `00/84`, the `05/*`
   profile reads and `06/*` sensor reads
2. `00/04` = `03` (driver mode)
3. `00/05`, then DPI: `04/05`, `04/06`
4. 44 × `02/0C` — every control × both layers (22 records each way)
5. `06/8E` (sensor query)
6. `00/B3` — commit
7. continuous `0F/03` lighting frames

The mapping table push in step 4 is what revealed the record format; see the
research log for how that capture was obtained.

## Nothing to write home about: the gotchas

* **Transaction id.** `0xFF` on interface 0 resets the device. Still the most
  common way to "break" the mouse from Linux.
* **Writes are trusted.** The firmware performs little validation: an
  unknown command with plausible framing can be accepted and destroy state.
  A `02/04` write on this test device zeroed the 22-byte `02/84` record and
  changed what `02/82` returns; the button map was not affected because it
  lives elsewhere, but the damage survived power cycles. Capture first, write
  second, and keep a backup of any bytes you read.
* **Commit.** A write without `00/B3` sits in flash until the next
  enumeration.
* **Exclusive access.** The mouse can only be claimed by one process. A VM
  with USB passthrough or a hidraw daemon will make claims fail.
* **Claiming input interfaces** (1, 2) can leave the device without `hidraw`
  nodes until it is re-enumerated. Claim interface 0 only.
* **Reads after writes** need a short pause; bursts start echoing.
