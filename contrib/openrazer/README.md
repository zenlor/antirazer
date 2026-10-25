# OpenRazer patch — onboard button actions for the Naga LH 2020

This directory carries a patch that teaches [OpenRazer](https://openrazer.github.io/)
the mapping protocol antirazer uses, plus a NixOS overlay that applies it to
the OpenRazer packages from nixpkgs.

The patch adds, for the Razer Naga Left-Handed Edition 2020 (`1532:008d`):

* a `button_action` sysfs file in the `razermouse` kernel module (read/write),
* `razer.device.onboard` DBus methods `getButtons`, `getButtonAction` and
  `setButtonAction` in the daemon.

It does not touch DPI, polling or lighting, which OpenRazer already handles
for this device.

## Verification status

| part | state |
|---|---|
| patch applies to a clean OpenRazer v3.12.4 tree (nixpkgs) | yes |
| patched kernel module builds (nixpkgs 26.05, Linux 6.18.57) | yes |
| `razermouse.ko` contains the `button_action` attribute and helpers | yes |
| patched daemon package builds and contains the `razer.device.onboard` methods | yes |
| daemon method logic (validation, selector/action bytes, slicing) | standalone unit test passed |
| master variant applies to OpenRazer master (`477f4d6`, 2026-02-28) | yes, with context offsets |
| exercised on hardware | **not yet** — there was no USB access in the development environment |

The USB write bytes are the ones antirazer verified live on the device
(`02/0C` records plus the `00/B3` commit). The read-back decoding of
non-keyboard actions is still assumed rather than measured — see
*Known unknowns*.

## The API

`button_action` is write/read. Writing **three bytes** selects a button;
writing **five to ten bytes** selects it and stores an action:

```
<store> <button> <layer> [<type> <length> <data...>]
```

* `store` — always `0x01` here: the live profile the mouse runs. The LH 2020
  has no other profile addressed by this patch.
* `button` — control instance. `0x40`..`0x4B` are the twelve thumb-grid
  buttons, labelled `1`..`9`, `0`, `-`, `=` on the device.
* `layer` — `0x00` standard, `0x01` Hypershift.
* action — `<type> <length> <data...>`:

| action | bytes | meaning |
|---|---|---|
| default | `00 00` | device default for that button |
| key | `02 02 <mod> <usage>` | keyboard key; `mod` is the HID modifier bitmap, `usage` the HID keyboard usage |
| autofire | `0D 04 <mod> <usage> <rate hi> <rate lo>` | repeats while held; `rate = 1000000 / delay_ms`, big endian |

Reading returns the ten-byte record
`<store> <button> <layer> <type> <length> <data...>` for the button selected
by the last write.

The layout is byte-identical to antirazer's `naga-mapper` payloads and to the
encoding used by the Naga V2 Pro patch (PR #2962), which is why this patch
reuses its names.

## Using it — sysfs

```sh
# find the thumb-grid interface (the one with button_action)
SYS=$(for d in /sys/bus/hid/drivers/razermouse/0003:1532:008D.*; do
        [ -e "$d/button_action" ] && echo "$d"; done)

# select button 1 on the standard layer and read the stored action
printf '\x01\x40\x00' > "$SYS/button_action"
od -An -tx1 "$SYS/button_action"

# bind button 1 to Ctrl+Shift+T
printf '\x01\x40\x00\x02\x02\x03\x17' > "$SYS/button_action"

# bind button 1 to F5 as autofire every 3 seconds
printf '\x01\x40\x00\x0d\x04\x00\x3c\x01\x5e' > "$SYS/button_action"
```

Writes are committed automatically (`00/B3`), so the new binding is active
immediately, without a replug. Access is root or the `openrazer` group the
udev rules create.

## Using it — DBus

The daemon exposes the same operations on the usual device object path:

```sh
busctl --user tree org.razer                      # find /org/razer/device/<serial>
busctl --user call org.razer /org/razer/device/<serial> \
  org.razer.razer.device.onboard getButtonAction yyy 1 64 0
busctl --user call org.razer /org/razer/device/<serial> \
  org.razer.razer.device.onboard setButtonAction yyyay 1 64 0 4 2 2 3 23
```

`pylib` wrappers are **not** part of this first milestone; the methods are
callable over DBus as shown.

## NixOS overlay

The flake exports the overlay as `overlays.default`:

```nix
{
  inputs.antirazer.url = "git+https://git.sr.ht/~zenlor/antirazer";

  outputs = { self, nixpkgs, antirazer }: {
    nixosConfigurations.machine = nixpkgs.lib.nixosSystem {
      system = "x86_64-linux";
      modules = [
        { nixpkgs.overlays = [ antirazer.overlays.default ]; }
        { hardware.openrazer.enable = true; }
      ];
    };
  };
}
```

It overrides `pkgs.python3Packages.openrazer-daemon` and
`pkgs.linuxPackages.openrazer`. If you boot another kernel package set (for
example `linuxPackages_latest`), extend that set with the same patch or point
`hardware.openrazer.packages.kernel` at a patched build.

## Without Nix

```sh
git clone https://github.com/openrazer/openrazer
cd openrazer
git checkout v3.12.4
git apply /path/to/0001-razermouse-add-onboard-button-actions-v3.12.4.patch
# then build/install driver + daemon as OpenRazer documents
```

## Patch files

| file | base | used for |
|---|---|---|
| `patches/naga-lh-2020-driver.patch` | v3.12.4 | kernel module in the overlay |
| `patches/naga-lh-2020-daemon.patch` | v3.12.4 | daemon in the overlay |
| `0001-razermouse-add-onboard-button-actions-v3.12.4.patch` | v3.12.4 | applying the whole change by hand to a full checkout |
| `0001-razermouse-add-onboard-button-actions-master.patch` | master `477f4d6` | upstream discussion |

The split files exist because nixpkgs builds the daemon with `sourceRoot =
…/daemon`; a patch touching `driver/` cannot apply there. The two together are
identical to the full v3.12.4 patch.

The two full patches contain the same change. Master moved enough code that
the release patch needs `patch --fuzz` (offsets only) there, so the master
variant was regenerated rather than relying on fuzz.

## Relation to upstream PR #2962

[PR #2962](https://github.com/openrazer/openrazer/pull/2962) adds the same
`button_action` sysfs file and `razer.device.onboard` interface for the Naga
V2 Pro, including macros and side plates. This patch deliberately reuses its
names, argument layout and action encoding so the two can live together: if
that PR lands first, the LH 2020 contribution shrinks to the device case in
probe/disconnect, the daemon class methods, and the read request's input-class
argument described below.

## Known unknowns

* **The read request needs the input class.** The LH 2020 does not answer
  `02/8C` unless argument 3 is the control's input class (`0x02` for the
  thumb grid). The patch hardcodes `0x02` because only thumb buttons are
  exposed. Other button families need their own class (`01` for main buttons,
  `06` for DPI keys) before they can be read.
* **Read-back of non-key actions is assumed, not verified.** The daemon slices
  the action out of the reply at byte 3, matching the write layout. For a
  keyboard binding that is consistent with the reads antirazer did on the
  device; autofire read-back has not been confirmed.
* **Only the live profile and the thumb grid are exposed.** Storage ids 2-5,
  main buttons, wheel and DPI keys are deliberately rejected by the driver's
  validation until their write behaviour is verified on hardware.
* **Macros are not included.** Synapse runs macros host-side for this
  generation; the firmware stores only references. The daemon's existing
  host-side macro methods are unrelated and untouched.
