<!--
Copyright 2026 RDK Management
SPDX-License-Identifier: Apache-2.0
-->

# App-usage grounding

The CORE suite tests the Rialto interface *as defined* — the public headers and
the reference component tests. This note records what deployed player
applications actually drive, so the suite's relevance to real usage is auditable.
It is derived from a code-level survey of the shipping application stack; the
survey itself names specific applications and SoC vendors and so lives only in
the private feed. The findings below are the source-neutral distillation, cited
to the `RC-*` rows they correspond to.

## Findings

### Surface A is the app-facing contract; Surface B is its dependency
Deployed players drive playback through the **GStreamer MSE sinks**
(`rialtomse{audio,video}sink`, Surface A) — they install the sinks into a
`playbin`/`appsrc` graph and set element properties. No shipping player drives
the native `IMediaPipeline` (Surface B) directly; that surface is exercised
internally by the sinks. The suite covers both, and both matter — the sinks
depend on the native contract — but the **app-facing priority is Surface A**.
Reflected in the matrix: the `path: mse` rows are the app contract; the
`path: native` rows are the contract the sinks rely on.

### The universal property set is small and placement-centric
Nearly every player sets only: window geometry (`rectangle`), video-mute
(`show-video-window`), `frame-step-on-preroll`, `volume`/`mute`, and the
`appsrc` feed parameters (`format`, `max-bytes`, `min-percent`). Everything else
is application- or platform-specific. Each of the universal properties maps to a
covered row:

| Universal property | Covered by |
|---|---|
| `rectangle` (geometry) | RC-CORE-MSEPROP-006 / RC-CORE-PIPE-015 |
| `show-video-window` (video mute) | RC-CORE-MSEPROP-008 |
| `frame-step-on-preroll` | RC-CORE-MSEPROP-006 |
| `volume` / `mute` | RC-CORE-MSEPROP-003 / RC-CORE-PIPE-017/018/019 |
| `appsrc` feed (`format`/`max-bytes`/`min-percent`) | the data-path harness drives this; RC-CORE-DATA-001/003/006 |

### Caps carry the stream, and the fields players set are covered
Players attach codec media-type caps and the sink reads
`width`/`height`/`framerate`/`codec_data`/`stream-format`/`alignment`/DV fields
from the incoming caps to build the server source — covered by
**RC-CORE-MSECAPS-006** (with DV/raw-audio as the platform-variable slice).

### Version skew — the suite pins an older sink than the deployed stack
The suite targets **rialto-gstreamer v0.20.1** (`framework.lock`). The deployed
application stack drives a **later** rialto-gstreamer whose video sink exposes
additional properties the pinned release does not — `report-decode-errors` and
`queued-frames` — and at least one is set by a shipping player. So the pinned
property surface **lags** the deployed one: the suite cannot currently test
properties real players set. Tracked as a suite-target decision (see the pin
evaluation) rather than an interface question — the release the suite targets is
a deliberate, single-release choice (invariant #6), so closing the skew means
consciously advancing the `framework.lock` pin and adding the new-property cases.

### Properties with no Rialto MSE sink equivalent
Players set several properties on their platform video sink that the
`rialtomse*sink` does not expose — see interface-definition-gaps.md **IDG-007**.
