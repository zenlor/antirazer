# vm — Windows VM harness for Synapse captures

A QEMU harness for letting Razer Synapse write to the mouse while the host
records the USB traffic. See [`../../docs/vm-capture.md`](../../docs/vm-capture.md)
for the full procedure; this file is the quick reference.

You must supply your own Windows ISO, licence and Synapse installer.

## Requirements

`qemu-system-x86_64`, `swtpm`, OVMF (fetched through Nix), a SPICE client, and
`socat` for the monitor helpers. `pnmtopng` (netpbm) or `magick` for
screenshots, `sshpass` for `guest-ssh.sh`.

## State

Everything lives outside the repository:

| variable | default |
|---|---|
| `NAGA_VM_DIR` | `~/.local/share/antirazer/vm` (disk, OVMF vars, TPM, monitor socket) |
| `NAGA_VM_SPICE_PORT` | `5900` |
| `NAGA_VM_SSH_PORT` | `2222` |
| `NAGA_VM_RDP_PORT` | `3389` |
| `NAGA_VM_USER` | `naga` |
| `NAGA_VM_PASS` | unset — required by `guest-ssh.sh` |
| `NAGA_VM_STAGE` | unset — required by `guest-python.sh` |
| `NAGA_VM_PYTHON` | `C:\Python312\python.exe` |
| `NAGA_VM_WORKDIR` | `C:\RazerPkg` |

## Scripts

| script | purpose |
|---|---|
| `run-windows-vm.sh [iso]` | boot installer or installed Windows; SPICE on 5900, monitor socket in `$NAGA_VM_DIR` |
| `guest-ssh.sh "cmd"` | run a command in the guest (needs an SSH server and `NAGA_VM_PASS`) |
| `guest-python.sh ["stmt" \| script file.py]` | stage a Python snippet over HTTP and run it in the guest, for UI automation |
| `key.sh <combo>` | send one key combination over the monitor |
| `type.sh "text"` | type ASCII text over the monitor (`DBG=1` prints the commands) |
| `screenshot.sh [out.png]` | screendump the guest to PNG |

## Quick start

```sh
./run-windows-vm.sh /path/to/windows.iso
remote-viewer spice://127.0.0.1:5900     # install Windows + Synapse

# stage python automation (in another terminal)
export NAGA_VM_STAGE=/tmp/vmstage
mkdir -p "$NAGA_VM_STAGE" && (cd "$NAGA_VM_STAGE" && python3 -m http.server 8000)

export NAGA_VM_PASS=...                   # guest password
./guest-python.sh "d.click(100, 200)"
./type.sh "hello"
./screenshot.sh /tmp/guest.png
```

In the SPICE client's USB menu, tick the Naga to move it into the guest and
untick it afterwards. Never use host-level USB passthrough for the only mouse
on the desk.

## Caveats baked into the scripts

* `-machine kernel_irqchip=split` — the default in-kernel irqchip can leave
  the guest without keyboard interrupts.
* `-spice port=...` rather than a display backend: nixpkgs QEMU has no SPICE
  display backend.
* Guest automation over SSH reaches a non-interactive session on Windows;
  a scheduled task started with `/IT` is the reliable way to reach the
  interactive desktop.
* The guest's HTTP client caches aggressively — use unique file names when
  staging scripts (this is why `guest-python.sh` writes a timestamped name).
