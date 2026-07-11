<!--
Copyright 2026 RDK Management
SPDX-License-Identifier: Apache-2.0
-->

# Interface-definition gaps & questions for upstream

The Rialto external interface has a well-defined **syntactic** contract — the
public `I*.h` headers and the IPC `.proto` schema, pinned per release
(`targetRialtoRelease`). Its **operational** contract (callback ordering, the
playback state machine, the need-data/have-data protocol, QoS semantics, the
EME/decrypt session lifecycle) is not stated in the headers but **is derivable
from the reference code the suite already builds against**: Rialto's shipped
component tests (`framework/rialto/tests/componenttests`) and unit tests carry
step-by-step behavioural narratives and encode the expected callback sequences as
assertions. Conformance cases for those behaviours cite that reference as their
oracle and are classified CORE (transform-safety: a replacement Rialto must
uphold the same observed contract).

This document tracks the **residual** items that the code cannot settle:
header contradictions, behaviour the reference neither implements nor exercises,
and intent questions. Each needs an authoritative answer from the interface
owner; each is cited to the `RC-*` requirement it affects. Resolutions feed back
into the catalogue and the matrix.

## Open items

### IDG-001 — `notifyDuration` parameter unit contradiction
`IMediaPipelineClient::notifyDuration(int64_t duration)`'s `@brief` states the
duration is "in nanoseconds" while its `@param` on the following line states
"in seconds". Affects **RC-CORE-DATA-011**.
**Question:** which unit is authoritative? (Nanoseconds is consistent with
`getDuration`, `notifyPosition`, and the `kDurationUnknown`/`kDurationUnending`
sentinels.)
**Recommended:** fix the `@param` to "nanoseconds" upstream.

### IDG-002 — `notifyDuration` is never emitted
The server contains no caller of `notifyDuration`, and Rialto's own component
tests never exercise it (v0.22.3). Duration is instead owned by the app.
Affects **RC-CORE-DATA-011**.
**Question:** is the duration-push callback intentionally reserved / app-owned
(so its absence is by design), or is this an unimplemented path?
**Impact:** determines whether RC-CORE-DATA-011 is recorded as *not applicable to
this release's server* or as a *gap*.

### IDG-003 — `getDuration` contract for a stream source
`getDuration` is `gst_element_query_duration(pipeline, GST_FORMAT_TIME)`, which
returns false for the stream-type appsrc that backs every MSE source (no
determinable pipeline duration; no public/IPC way to supply one). Affects
**RC-CORE-PIPE-013** (covered as the negative contract:
`L4PlaybackDataPathTests.GetDurationNotDeterminableForStreamSource`).
**Question:** confirm "returns false when the duration is not determinable" is the
intended contract for a live/stream source, rather than reporting a
`kDurationUnending`-style value.
**Impact:** ratifies the negative-contract reading; if a sentinel value is
expected instead, PIPE-013 becomes a gap.

### IDG-004 — Authority of the playback callback sequence
The playback callback sequence (`IDLE → PAUSED → PLAYING → END_OF_STREAM →
STOPPED`, and the need-data/have-data protocol) is specified by Rialto's
component tests and the RDK "Rialto Playback Design" sequence diagrams, not by
the public headers. Affects the **RC-CORE-DATA-\*** rows.
**Question:** is that sequence the guaranteed external contract (safe to assert
in conformance), or implementation-incidental?
**Impact:** the suite treats it as the CORE transform-safety contract with this
provenance noted; an authoritative confirmation removes the "derived, not stated"
caveat.

### IDG-005 — `stop()` STOPPED notification is not deliverable-by-construction
The STOPPED playback-state notification is emitted only from a GStreamer bus
state-changed(NULL) message (`HandleBusMessage`). On a live pipeline,
`gst_element_set_state(NULL)` is synchronous and — with GstPipeline's default
`auto-flush-bus` — flushes the bus while the server's dispatcher thread polls it
(`gst_bus_timed_pop_filtered`, 100 ms cadence), so the message is usually dropped
and the client never receives STOPPED. Rialto's own component tests observe
STOPPED only by injecting the message through a mocked gst wrapper. Confirmed
empirically on the software platform: after `stop()` from PAUSED, no STOPPED
notification arrives within 15 s. Affects **RC-CORE-PIPE-009**.
**Question:** is the STOPPED notification part of the guaranteed contract (in
which case emission must not depend on the flushed bus message), or is `stop()`'s
contract the synchronous return only, with STOPPED best-effort?
**Impact:** the suite asserts the synchronous MUST contract (returns true, must
not block) and logs, without asserting, STOPPED delivery.

### IDG-006 — `closeKeySession(unknown id)` returns FAIL, not BAD_SESSION_ID
`IMediaKeys::closeKeySession`'s header documents BAD_SESSION_ID when "the
session id does not exist", but the reference implementation's service layer
(`CdmService::closeKeySession`) rejects an unknown id from its own session
registry with FAIL before the BAD_SESSION_ID-returning session lookup is
reached. The sibling operations (generateRequest / updateSession /
removeKeySession) do return BAD_SESSION_ID. Affects **RC-CORE-KEYS-006**.
**Question:** should closeKeySession return BAD_SESSION_ID for an unknown id
(header), or is FAIL from the service registry the intended contract?
**Impact:** the suite asserts the observed reference contract (FAIL for close,
BAD_SESSION_ID for the siblings) as transform-safety, noting the discrepancy.

### IDG-007 — app-driven video-placement / secure / timecode properties have no `rialtomse*sink` equivalent
A code-level survey of deployed player applications (private feed) shows players
set several properties on their platform video sink that the Rialto MSE sink does
not expose: video-plane **z-order**, **zoom/scaling mode**, **secure-video**
signalling, and SEI **timecode** extraction. The `rialtomsevideosink` exposes
window geometry (`rectangle`) but no equivalent for those: secure decode is
negotiated server-side / through encrypted caps rather than a sink property, and
z-order / zoom / timecode have no sink property at all. This is derived from
real usage, not a suite case (see coverage/app-usage-grounding.md).
**Question:** are video-plane placement beyond `rectangle` (z-order, zoom) and
timecode extraction intended to be outside the Rialto MSE sink contract (an
app / compositor / server responsibility), or are they gaps that surface as
players migrate onto the Rialto sinks?
**Impact:** determines whether these become covered rows (if in-contract) or are
documented as deliberate non-goals; today they are neither tested nor promised.

### IDG-008 — the common-vs-platform-specific property boundary is undefined (`getSupportedProperties` is registry-dependent)
The value of migrating an application onto Rialto is a **common, platform-
independent external interface**: the app stops driving its own (often platform-
specific) sink properties and relies on the property set Rialto guarantees on
every target. That guarantee requires a **defined common property set** — the
properties present on every Rialto target, testable unconditionally (absence is a
failure). Everything else is a **platform-specific extension**, present only where
the backend supports it and never something a portable app may depend on. The
boundary between the two is the portability contract.

Today that boundary is not defined, because the query meant to describe it is
registry-dependent. `getSupportedProperties(mediaType, names)` returns a name if
**any** GStreamer element factory of that media type registered *at call time*
installs a property of that name (`GstCapabilities::getSupportedProperties` scans
the live factory list). Two consequences, observed cross-surface
(RC-CORE-CONSIST-005), reproducibly on every gate run:

1. **Not surface-accurate.** It reports a property the `rialtomse*sink` does not
   expose — `native getSupportedProperties(AUDIO)` returns `sync`, yet
   `rialtomseaudiosink` has no `sync` `GParamSpec` (the sink derives from
   `GST_TYPE_ELEMENT`; `sync` is carried by other platform audio elements the scan
   also sees). The backend nonetheless honours `sync` (server `SetupElement` sets
   it), so this is not "sync is unsupported" — it is the interface disagreeing with
   itself about whether `sync` is part of the contract.
2. **Not deterministic.** The answer depends on registry state at call time. A sink
   queries once at class-init and installs the returned subset permanently; a
   property registering later (as plugins load over the process lifetime) is then
   reported supported by a native query but is absent from the sink forever.
   `audio-fade` is stable only because it has a dedicated always-on fallback in
   the scan.

So the native answer is a **superset** of, and can drift from, what any sink
actually exposes. The assertable cross-surface invariant is therefore only that a
sink's installed optional set is a **subset** of the native answer (a sink must
not expose a knob the platform disowns), not equality — RC-CORE-CONSIST-005.
**Decision (this suite):** a platform-independent conformance interface must be
stable and surface-accurate, so the undefined boundary is recorded as a **gap**
(matrix `gaps:`), not left as an open style choice.
**Question (upstream):** (a) which optional properties are part of the **common**
Rialto interface (guaranteed on every target) versus platform-specific extensions
— i.e. is `sync` common or an extension? (b) Is `getSupportedProperties` intended
as a stable per-source contract, or a "does any element currently support this"
probe? If a contract, the scan must be pinned to a defined element set (or the
common set specified directly) so every caller — the sink at class-init and an app
later — gets the same answer.
**Impact:** settling (a) defines the portable property surface the suite tests
unconditionally; settling (b) makes native/sink set-**equality** testable instead
of only the subset guard. Filed upstream against rdkcentral/rialto.
