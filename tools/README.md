# tools — protocol research utilities

These are the tools used to decode the mapping protocol. They are not needed to
configure the mouse — that is `naga-mapper` and `naga-ui` at the top of the
repository. They are published because the protocol is not fully mapped yet
(profiles, assignment types, lighting) and because reproducing the evidence
should not require reinventing the rig.

**Writes can be destructive.** The firmware trusts what it is given: an
unknown command with plausible framing may be accepted and persist. Read the
[protocol gotchas](../docs/protocol.md#nothing-to-write-home-about-the-gotchas)
and the [research log](../docs/research-log.md) before sending anything.

| tool | purpose |
|---|---|
| [`naga-probe.c`](naga-probe.c) | send one arbitrary 90-byte feature report, listen on IN endpoints, or GET-scan the command space |
| [`evtap.c`](evtap.c) | dump EV_KEY/EV_REL events from evdev nodes to see what the firmware emits natively |
| [`bench-remap.c`](bench-remap.c) | loopback latency benchmark for `naga-remap` (cold startup and trigger→uinput hot path) |
| [`captures/`](captures/) | capture live USB traffic and analyse Synapse captures |
| [`vm/`](vm/) | Windows VM harness for obtaining ground truth from Synapse |

Build the C tools with:

```sh
nix develop -c make tools      # -> naga-probe, evtap, bench-remap
```

The binaries are git-ignored. `naga-probe` refuses to write unless
`NAGA_WRITE=1` is set; read-only use (`-S`, `-L`) needs no gate.

## Latency benchmark

`bench-remap` creates a virtual Naga 2014 uinput device, starts the real
`naga-remap run` against it with a `1` → `x` map, and reports:

* **cold** — fork/exec until the daemon has loaded the map, built its plans,
  grabbed the source and created its output uinput device;
* **hot** — trigger key written to the source uinput until the mapped key is
  readable on the output device, over N iterations (min/p50/p95/max).

```sh
./bench-remap --iterations 100
```

It needs `/dev/uinput` and read access to `/dev/input` (usually root or the
`input` group), so run it outside a restricted sandbox. The hot number
includes the kernel/uinput round trip and is therefore an upper bound for a
physical mouse; it is not a microbenchmark of `remap_plan_build()`.

Raw material — USB captures (`.pcapng`), VM disks, firmware/vendor files — is
not stored in this repository. The procedure to create it is in
[`docs/vm-capture.md`](../docs/vm-capture.md).
