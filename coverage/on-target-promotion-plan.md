<!--
Copyright 2026 RDK Management
SPDX-License-Identifier: Apache-2.0
-->

# On-target promotion plan

Nine `RC-CORE-*` rows are `planned` and two are `gap`. Each one waits on a
capability the Linux software platform does not have. This is what to run when a
target with that capability is available, and what to record from it.

Everything here is a matrix edit plus, where named, a case. The cases are never
edited to suit a target.

## How a row moves

A `planned` row becomes `covered` when a case asserts it against a target that
has the capability, and that case passes. Three things change together:

1. the case exists and is named in [matrix.yaml](matrix.yaml) `case:`
2. `status:` becomes `covered`
3. the `case:` prose — today a parenthetical saying what is missing — becomes the
   assertion the case makes

A row whose case **fails** on real hardware is a result, recorded as one: set
`status: finding`, state what was asserted, what the target did, and which it is
— a backend defect or a specification gap. A failing row is evidence about the
target or the interface. It is never softened into a skip, and the case is never
relaxed to make it pass.

A row that the target's own capability profile gates off self-skips through
`CONFORMANCE_REQUIRE_CAP` and stays `planned` for that target. Record the HFP key
that gated it, so the next target with the capability picks it straight up.

## What each row needs

### A vendor CDM — KEYS-004, KEYS-007, KEYS-008, KEYS-010

The ClearKey backend in [backends/opencdm/clearkey](../backends/opencdm/clearkey)
is a real W3C CDM, and that is its limit: temporary sessions, no DRM header, no
renewal, and a state machine that accepts `update` wherever the spec allows it.
These four assert what a licensed CDM does differently.

| Row | Asserts against a vendor CDM |
|---|---|
| KEYS-004 | the DRM-header path (PlayReady-specific) |
| KEYS-007 | `update` in a wrong state is rejected by a CDM whose state machine enforces it |
| KEYS-008 | an unsupported key-system type returns `NOT_SUPPORTED` rather than failing construction |
| KEYS-010 | license renewal on a persistent session |

Target: one with Widevine or PlayReady provisioned. The same target promotes
`MSECAPS-006` (DV/HEVC) and the two `KEYS-*` capability skips already in the
5-skip set.

### A sanitizer, not a server — DATA-008

A segment's data buffer must stay valid until the matching `haveData` completes.
That is an obligation on the client, so no server response proves or disproves
it: it is caught by running the suite under AddressSanitizer, where a use of the
buffer after the feed released it is reported at the point it happens.

The other fault clauses that stood here — `NO_SPACE`, an unexpected call, a
cancelled request, a non-fatal playback error — are covered:
`L4DataFaultTests` reaches all four from the public API with a feed that
deliberately misbehaves. Two of them assert the always-true clause and record a
platform-dependent announcement, which firms up on a target that makes it: a
server that cancels on flush, and a decoder that reports a dropped frame rather
than concealing it.

### A native VIDEO feed — PIPE-016

The property lives on the video sink, so the case needs a native VIDEO source —
`MediaSegmentVideo` over `generateH264AvcStream`, the counterpart to the audio
feed already in the DATA batch. It rides with a target that renders video, and
carries the video QoS assertions with it.

### A subtitle-capable target — PIPE-020, PIPE-027

Both need a SUBTITLE source and a platform text-track service. The software
platform stubs `TextTrackPluginWrapper`, so the capability is platform-variable:
gate both on the target's HFP declaring it.

### An upstream answer — DATA-011

The server never calls `notifyDuration` at v0.24.0 (IDG-002). A target that does
call it promotes the row to `covered`. A target that does not turns IDG-002 into
a confirmed interface gap across two implementations, which is what the upstream
question needs. Either way the run settles it.

### The two gaps

`com.apple.fps` needs a target that provisions FairPlay. **IDG-008** — the
`getSupportedProperties` registry scan, filed as
[rdkcentral/rialto#557](https://github.com/rdkcentral/rialto/issues/557) — needs
an authoritative statement of the common-versus-platform-specific property
boundary; a second platform's property set is evidence for it, not a resolution
of it. Record what the target advertises against
[cross-surface-fact-inventory.md](cross-surface-fact-inventory.md).

## Running it

```bash
./build.sh                                   # or ./sc-build.sh for the software platform
./test.sh --slot linux-native                # the whole CORE gate
./test.sh --slot linux-native --scope L4     # the data path on its own
./test.sh --slot linux-native --tier all     # CORE + EXTENDED
```

Point the slot at the box by editing its four marked console fields in
[../raft/rack_config.yml](../raft/rack_config.yml). Its `platform` selects the
entry in [../raft/device_config.yml](../raft/device_config.yml), which names the
deploy mode, the launch command and the URL of that platform's HFP — the
capability profile deciding which of these rows the target even offers.

Prove the path first with the emulator (`./emulator.sh up`, then
`./test.sh --slot linux-emulator`): it is the same flow over the same ssh hop, so
anything that breaks there is the harness rather than the target.
