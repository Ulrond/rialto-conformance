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
