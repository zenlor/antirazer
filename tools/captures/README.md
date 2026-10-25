# captures — capture and analyse USB traffic

Requires `tshark` (with USB support) and Python 3.9+. The dev shell provides
both, so run the scripts inside it (`nix develop -c python3 ...`). Capturing
from usbmon needs root or the right capabilities; reading a capture usually
needs the same.

Two environment variables make the scripts reusable across machines:

| variable | default | meaning |
|---|---|---|
| `NAGA_DEV` | `8.16.0` | the device address as `tshark` prints it (`bus.device`, e.g. `8.16.0`) |
| `TSHARK` | `tshark` | path to the tshark binary |

Find the address of an existing capture with:

```sh
tshark -r capture.pcapng -Y 'usb.idVendor == 0x1532' -T fields -e usb.src -e usb.dst | head
```

## Live capture

| script | usage |
|---|---|
| `capture-usb.sh` | `sudo ./capture-usb.sh out.pcapng [seconds]` — finds the Naga's bus via sysfs and records usbmon until the timeout (or Ctrl-C) |

Load the module first: `sudo modprobe usbmon`.

## Analysis

| script | usage | output |
|---|---|---|
| `analyze.py` | `./analyze.py cap.pcapng...` | every Razer SET_REPORT grouped by tid/class/cmd/size, with distinct argument sets; lighting counted separately |
| `allwrites.py` | `./allwrites.py cap.pcapng` | all control-OUT transfers with setup packet and raw data, no framing assumptions |
| `baseline_reads.py` | `./baseline_reads.py cap.pcapng` | class-02 (mapping domain) transfers grouped by command and size |
| `mappingtable.py` | `./mappingtable.py cap.pcapng` | the `02/0C` mapping-table push, one line per record with ctr and layer |
| `records.py` | `./records.py cap.pcapng...` | decode every `02/0C` record (keyboard, mouse, autofire, macro, layers) |
| `records.py` | `./records.py --diff before.pcapng after.pcapng` | the bindings that changed between two captures |
| `ceremony.py` | `./ceremony.py cap.pcapng` | the whole conversation in time order, split into phases, repeats collapsed |
| `catalog.sh` | `./catalog.sh cap.pcapng` | one line per SET_REPORT decoded via `tshark -V` |
| `dump-control.sh` | `./dump-control.sh cap.pcapng 8.16.0` | control-transfer setup fields and payloads |
| `in-ep.sh` | `./in-ep.sh cap.pcapng 8.16.2` | unique interrupt-IN payloads with counts |

`catalog.sh` exists because `tshark -T fields` cannot be relied on for the
setup fields of these captures; it parses `-V` output instead.

## Typical session

```sh
# with the mouse shared into the Windows VM, before touching Synapse
sudo ./capture-usb.sh /tmp/baseline.pcapng 60

# remap one button in Synapse, then
sudo ./capture-usb.sh /tmp/change.pcapng 30

python3 ceremony.py /tmp/baseline.pcapng      # what Synapse does on connect
python3 analyze.py /tmp/change.pcapng         # what a remap looks like
python3 mappingtable.py /tmp/baseline.pcapng  # the record format
python3 records.py --diff /tmp/baseline.pcapng /tmp/change.pcapng  # changed bindings
```

`records.py --self-test` checks the decoder and diff logic without a capture.
Its diff takes the last write per (store, ctr, layer), so it is best to compare
two captures that each contain a full table push (see the reconnect step in
the capture guide).

For capturing a specific feature — mouse-button actions, the Hypershift
switch, layer-2 bindings — follow
[the procedure in the VM guide](../../docs/vm-capture.md#4-capturing-mouse-buttons-and-hypershift).

See [`../../docs/vm-capture.md`](../../docs/vm-capture.md) for the VM setup and
[`../../docs/protocol.md`](../../docs/protocol.md) for the decoded result.
