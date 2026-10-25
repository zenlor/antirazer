# Research log — how the protocol was decoded

A condensed and cleaned-up record of the reverse-engineering sessions, kept so
the next person does not repeat the dead ends. For the resulting protocol see
[`protocol.md`](protocol.md); for the capture rig see
[`vm-capture.md`](vm-capture.md).

Raw material (USB captures, VM images, vendor installers) is deliberately not
in this repository. It is large, machine-specific and partly vendor data; the
procedure to regenerate it is documented instead.

## Starting point

The Naga Left-Handed Edition 2020 (`1532:008d`) does not work with OpenRazer
beyond basic mouse input: the LS/Naga entry sends its payloads to interface 0
with transaction id `0xFF`, while this firmware only answers `0x1F`, and the
mouse resets instead. No public driver implements button remapping for this
device — the macOS projects (NagaController, OpenNaga) intercept events on the
host and never speak USB.

Public captures in the OpenRazer issue tracker (DPI and lighting changes) did
establish the general frame: 90-byte reports, class/cmd/size, XOR CRC, and the
`0x1F` transaction id. They contained no button-remap command, which at first
suggested Synapse might remap buttons in software. It does not: the map lives
in the mouse's flash.

## 2025-10-24 — first session

**Response routing.** SET followed by GET echoed the request (status stayed
`00`) and the dedicated IN endpoint on the LampArray interface stayed silent.
The cause was not transport but parameters: the device only answers on
transaction id `0x1F` and on interface 0.

**Interfaces.** Interface 1 SET disconnects the device instantly. Interface 3
(a 63-byte feature report used for lighting) answers, but never for config.
Interface 0 with `0x1F` is the channel.

**Read-only scan.** With GET-style requests on interface 0:

* class `02` is the settings/mapping domain; `02/84` returned a 22-byte blob of
  HID usages, at the time assumed to be the button table
* class `04` is DPI (`04/85` returned the live 1400/1400, `04/86` the
  five-stage table)
* class `0F` is lighting (three LED groups, matrix dimensions)
* class `05` looked like per-profile blocks
* `00/81` firmware version, `00/82` serial, `00/84` device mode, `00/85`
  polling rate

The serial number is not reproduced here.

**The incident.** Under the assumption that `02/84` was the keymap, a write was
attempted with `02/04` and the 22-byte blob minus its length prefix. The
firmware accepted it, and `02/84` has read back as zeros ever since. `02/82`
also changed from `08 1C 08 1C` to returning the serial string, so class `02`
response routing for that pair was clobbered. The button map was unaffected
across power cycles, proving `02/84` was not the live keymap.

Preserved blob, for restoration attempts:

```
16 01 02 03 34 35 0B 0C 0E 40 43 46 49 41 44 47 4A 42 45 48 4B 09 0A
```

Lessons that shaped the tool: never write an unverified format to the daily
driver; an echoed request can still be *accepted*; and reads are cheap, writes
are not.

**Synapse captures (public).** Dissecting the pcaps attached to the OpenRazer
issue confirmed:

* all commands use transaction id `0x1F`
* lighting goes to interface 2 as `0F/03` rows (6 rows × 21 columns)
* interface 2 also carries 22-byte asynchronous button/stage events
* still no remap command — the captures only changed DPI and lighting

## 2025-10-24/25 — Windows VM and ground truth

The only writer of the mapping table is Synapse on Windows, so a capture VM was
built: QEMU with OVMF + TPM, SATA disk, user-mode networking with SSH/RDP
forwarded, SPICE display, and the mouse shared into the guest over usbredir
(never host passthrough — it is the only mouse on the desk).

* `-machine kernel_irqchip=split` was needed or the guest lost keyboard
  interrupts. See [`vm-capture.md`](vm-capture.md) for the setup.
* Synapse 3.11 was installed offline and the Naga module registered; the
  mouse then appeared as configurable hardware.
* Capturing with usbmon on the host while remapping a button in Synapse
  produced the missing command: a burst of 44 `02/0C` writes (22 controls ×
  2 layers), then `00/B3`.

**Record format.** Ten argument bytes:
`01 <ctr> <layer> <class> <type> <mod> <usage> <…>`, with a keyboard assignment
as type `02`, modifier bitmap and HID usage. `ctr 40..4B` are the twelve thumb
buttons. `00/B3` commits.

**Live verification.** Writing letters `a`–`l` to buttons 1–12 and committing
made all twelve emit their letter immediately, no replug, no Windows. The digit
layout was restored the same way. Modifiers were verified with a Shift+key
assignment; the modifier byte is the standard HID bitmap.

**Autofire.** A capture of Synapse's autofire toggle showed assignment type
`0D` with `04` in the following byte and a big-endian rate
(`1000000 / delay_ms`).

**Static analysis.** Decompiling Synapse's mapping module revealed 165
assignment types (`MAP_MACRO`, `MAP_HYPERSHIFTKEY`, `MAP_DPI_*`,
`MAP_LEDCOLOUR`, …). Only the numeric values for keyboard and autofire have
been matched to the device so far.

**Host-side hypotheses, settled.** Buttons emit ordinary keyboard usages
(f1..f12, keypad, digits) because the firmware replays the stored mapping —
not because a driver remaps them. The map survives a power cycle and works on
a machine that never ran Synapse, so it is onboard.

## 2026-10-25 — mouse buttons and the Hypershift action

Re-running the original Synapse captures through
`tools/captures/records.py` answered the remaining action-type questions
without another VM session.

The connect push contains the full table: 22 controls × 2 layers.

* **Mouse buttons** are type `01`, length `01`, data = button id (`1` left,
  `2` right, `3` middle, `9`/`10` for the extra buttons). This is the same
  encoding as the Naga V2 Pro (OpenRazer PR #2962) and the Basilisk Ultimate
  captures.
* A deliberate edit in the second capture wrote `01 40 00 0b 01 01` (thumb
  button 1): type `0B`, length `01`, data `01`. `0B` is the only captured
  family not otherwise accounted for, and the decompiled enum contains
  `MAP_HYPERSHIFTKEY`, so this is the **Hypershift switch**. Whether it acts
  as hold-to-switch or a toggle is still unobserved.
* The **DPI keys** use type `06` (data `01`/`02`), the **profile key** type
  `07` (data `04`), and the **wheel tilts** type `0E`.
* Layer-`01` records exist for every control and use the same action encoding
  as layer `00`.

The record layout is `01 <ctr> <layer> <type> <length> <data...>`. The
`class/type/mod/usage` grouping in the first write-up was a misreading — for a
keyboard binding the type byte (`02`) and the length byte (`02`) happen to be
equal, so the fields looked interchangeable. The write path was already
correct; the read decoder took the type from the length byte and is corrected
together with the mouse support.

## State of the test device

The device used for development has its own layout in flash: thumb buttons were
returned to their digit bindings, some digit records were touched by test
writes, and the `02/84` record is still zeroed from the 2025-10-24 incident.
That is harmless for mapping work but worth knowing when comparing captures.

## Open questions

1. Writing new actions to the main buttons, DPI keys, profile key and wheel
   tilts. Their default records show the encoding, but changing them is
   untested.
2. Whether the Hypershift key acts as hold-to-switch or a toggle, and how it
   interacts with profiles.
3. Numeric values for the remaining assignment types (`MAP_MACRO`, LED
   actions, launch program, …).
4. Reading non-keyboard records back: the reply layout is assumed to mirror
   the write record, and the reply's third byte behaves as a flag for
   Hypershift keys.
5. The profile domain (`05/*`) and the 22-byte `02/84` record.
6. Lighting: the classic frame format is known, the extended matrix frames and
   their colour model are not.

## Working habits that paid off

* Read-only probing first; write only what a capture has shown.
* Keep the mouse on a stable port; a flaky hub produces phantom failures.
* Capture, diff two changes, and only then write a tool.
* Test writes on a layout you can restore, and record the original bytes
  before touching anything.
