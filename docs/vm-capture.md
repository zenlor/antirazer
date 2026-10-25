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

Connect a SPICE client to `spice://127.0.0.1:5900` (for example
`remote-viewer`). Then install Windows and Synapse in the guest as usual.

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
sudo ./tools/captures/capture-usb.sh /tmp/baseline.pcapng 60
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

## 4. Guest automation (optional)

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

## 5. Safety

* Do not write anything to the device that a capture has not shown. An
  accepted-but-wrong write can persist in flash (see the incident in the
  [research log](research-log.md)).
* Keep the mouse on a stable port, and note that the configuration channel
  resets the device for a few seconds if it receives the wrong transaction id.
* Read the mapping table before and after; it is the cheapest backup you will
  ever take.
