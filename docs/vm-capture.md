# Capturing Synapse ground truth in a Windows VM

The mapping protocol was recovered by letting Razer Synapse do the writing and
watching the USB traffic. This document describes the rig so the exercise can
be repeated for the parts that are still unknown (profiles, other assignment
types, lighting).

You need: a Windows installation ISO and licence, a Synapse installer from
Razer, QEMU, a SPICE client, `tshark`/`usbmon`, and a second pointing device —
the mouse under test is shared with the guest while capturing, so you cannot
use it on the host.

## 1. Create the VM

[`../tools/vm/run-windows-vm.sh`](../tools/vm/run-windows-vm.sh) boots a
q35/OVMF machine with TPM 2.0, one SATA disk, user-mode networking with SSH
(`127.0.0.1:2222`) and RDP (`127.0.0.1:3389`) forwarded, a SPICE display on
port 5900, a QEMU monitor socket at `vm/monitor.sock`, and four usbredir
channels.

```sh
./tools/vm/run-windows-vm.sh /path/to/windows.iso     # first run: installer
./tools/vm/run-windows-vm.sh                          # later runs: installed system
```

The script keeps its state in `$NAGA_VM_DIR` (`~/.local/share/antirazer/vm` by
default); point it at an existing VM directory to reuse one. Connect a SPICE
client to `spice://127.0.0.1:5900` (for example `remote-viewer`). Then install
Windows and Synapse in the guest as usual.

Two details cost hours during development and are baked into the script:

* `-machine kernel_irqchip=split` — with the default in-kernel irqchip the
  guest received no keyboard interrupts (mouse worked), which looks like a
  broken installer.
* `-spice port=5900` rather than a default display backend, because the
  nixpkgs QEMU has no SPICE display backend.

## 2. Share the mouse

In the SPICE client's *USB device selection* menu, tick the Naga. All four
interfaces move into the guest. Do **not** use host-level USB passthrough:
the mouse leaves the host, and getting it back means unplugging it.

When you are done, untick it again. The host then re-enumerates the device.

## 3. Capture

On the host, start a capture on the Naga's bus before you touch Synapse:

```sh
# tshark comes from the dev shell; sudo needs the path passed explicitly
TSHARK=$(nix develop -c bash -c 'command -v tshark' | tail -1)
sudo -E env TSHARK="$TSHARK" ./tools/captures/capture-usb.sh /tmp/baseline.pcapng 60
```

Then, in Synapse:

1. **Baseline**: open Synapse and change nothing. Synapse reads the whole
   configuration on connect and pushes the mapping table; this capture gives
   you the record format for free.
2. **One change**: start a fresh capture, remap a single button to a unique
   key (F20 is conveniently unused), save, wait a few seconds, stop.
3. **A second change**: remap it back, capture again. Diffing two captures
   separates the varying fields from the constants.

Analyse with [`../tools/captures/`](../tools/captures/):

```sh
python3 tools/captures/ceremony.py /tmp/baseline.pcapng     # full connect sequence
python3 tools/captures/analyze.py /tmp/remap-single.pcapng  # writes by class/cmd
python3 tools/captures/mappingtable.py /tmp/baseline.pcapng # the 02/0C push
```

The scripts expect the device address used by `tshark` (`bus.device`, e.g.
`8.16.0`); set `NAGA_DEV` if yours differs.

Capture the host side, not the guest: with usbredir the traffic passes through
QEMU on the host, so usbmon sees it.

## 4. Capturing mouse buttons and Hypershift

Synapse rewrites the whole mapping table when it connects, so the device's
current bindings are replaced by whatever Synapse last saved. Note the current
layout first (with the mouse on the host: `naga-mapper list >
~/naga-before.txt`) and expect to restore it afterwards.

Two ordinary usbmon captures are enough; `records.py` decodes the mapping
writes and diffs them:

1. **Baseline.** With Synapse closed (or the mouse still on the host), start
   capture A. Launch Synapse and/or share the mouse into the guest, then let
   it connect and push its table; leave about ten seconds of quiet.
2. **Mouse buttons.** Assign a different thumb button to each mouse-button
   action, so one table shows all of them: `1` → middle click, `2` → left
   click, `3` → back, `4` → forward. Save after each change and pause 2-3
   seconds between changes so the phases stay separable.
3. **Layer switch.** Assign another thumb button (`5`) to the Hypershift
   action. Save and pause.
4. **Hypershift layer.** Switch Synapse's layer selector to the Hypershift
   layer and bind one thumb button (`6`) to a keyboard key such as F13. Save
   and pause.
5. **Full table.** Start capture B, then unshare and reshare the mouse (or
   restart Synapse): Synapse pushes the complete table on reconnect, which
   makes the diff independent of how writes were batched. Stop both captures.
6. **Analyse**:

   ```sh
   python3 tools/captures/records.py --diff /tmp/baseline.pcapng /tmp/after.pcapng
   ```

   Each changed binding is printed with its action decoded. Anything the
   decoder does not know yet is shown as `type XX data ...` — that is what the
   new mouse-button and Hypershift actions look like until they are added to
   the decoder.

The capture should answer:

* the action type and parameter bytes for "mouse button" (left/right/middle/
  back/forward; the Naga V2 Pro patch suggests `01 01 <1..5>`),
* the action type and parameters for the Hypershift switch (the static
  analysis called it `MAP_HYPERSHIFTKEY`),
* whether a Hypershift binding's layer byte reads `01` on both layers,
* whether a Hypershift-layer binding uses layer byte `01` and the same action
  layout as a normal one,
* whether Synapse sends the `00/B3` commit after the writes.

Restore your own layout when done — with `naga-mapper set <ctr> <key>` for
keyboard bindings now, and with the mouse actions once they are implemented.

## 5. Guest automation (optional)

Driving the Windows GUI is the tedious part. The VM harness includes helpers
for it:

* [`../tools/vm/guest-ssh.sh`](../tools/vm/guest-ssh.sh) runs a command in the
  guest over SSH (set `NAGA_VM_PASS`; the guest needs an SSH server).
* [`../tools/vm/guest-python.sh`](../tools/vm/guest-python.sh) runs a Python
  snippet in the guest through a local HTTP server, intended for UI automation
  libraries (for example `windows-use`) installed in the guest.
* [`../tools/vm/type.sh`](../tools/vm/type.sh),
  [`../tools/vm/key.sh`](../tools/vm/key.sh) and
  [`../tools/vm/screenshot.sh`](../tools/vm/screenshot.sh) drive the QEMU
  monitor and need no guest cooperation at all — the fallback when SSH or the
  automation stack is broken.

Things learned the hard way:

* A scheduled task with `/IT` is the only reliable way to reach the
  interactive desktop over SSH; a plain SSH session sees a different session
  and UIA calls fail.
* The QEMU monitor accepts `sendkey`, `screendump` and friends; the helpers
  talk to `vm/monitor.sock`.
* Use unique file names when staging scripts into the guest; a small HTTP
  server in front of the guest will happily serve a cached copy of the
  previous script otherwise.

## 6. Safety

* Do not write anything to the device that a capture has not shown. An
  accepted-but-wrong write can persist in flash (see the incident in the
  [research log](research-log.md)).
* Keep the mouse on a stable port, and note that the configuration channel
  resets the device for a few seconds if it receives the wrong transaction id.
* Read the mapping table before and after; it is the cheapest backup you will
  ever take.
