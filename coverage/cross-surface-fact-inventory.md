<!--
Copyright 2026 RDK Management
SPDX-License-Identifier: Apache-2.0
-->

# Cross-surface fact inventory

Surface A (the `rialtomse*sink` GStreamer elements) and Surface B (the native
`IMediaPipeline` / capabilities client API) are two abstractions over one Rialto
backend. This document classifies every client-**observable fact** — every
getter, capability answer, read-back property, or query result — as exposed on
**Surface B only**, **Surface A only**, or **both**, and states, for each
both-surfaces fact, whether cross-surface agreement is assertable.

It is the map that scopes the L2 CONSIST cases: a both-surfaces fact is a
candidate for a `path: both` consistency row; an A-only or B-only fact is
correctly covered by a single per-surface row.

## The governing constraint

The two surfaces **cannot share one backend session** — each client owns its
own. So cross-surface agreement is assertable only for **session-independent**
facts: capability answers, template caps, and the published property contract
(installed `GParamSpec` set, defaults, ranges). **Per-session state** (position,
duration, live volume/mute, stats) is read from two independent sessions, so
"the two surfaces agree" is not an assertion the suite can make without an
identical driven stream on each — the per-surface rows own that state instead.

This is why RC-CORE-CONSIST-003 asserts the volume **contract** (range +
default), not a live volume read-back.

## Both surfaces — the overlap

| Fact | Surface B accessor | Surface A accessor | Session-indep? | CONSIST |
|---|---|---|---|---|
| Supported codec MIME/caps | `getSupportedMimeTypes(type)` | sink pad-template caps | yes | **001 ✓** |
| Video-master | `isVideoMaster(bool&)` | `is-master` (video, R) | yes | **002 ✓** |
| Volume contract | `getVolume`/`setVolume` | `volume` (audio + webaudio) | yes (contract) | **003 ✓** |
| Mute contract | `getMute(id,bool&)` | `mute` (audio + subtitle) | yes (contract) | **gap → 004** |
| Optional-property set | `getSupportedProperties(type,names)` | installed `GParamSpec`s | yes | **gap → 005** |
| Position | `getPosition(int64&)` | `GST_QUERY_POSITION` | **no** (per-session) | not assertable |
| Duration | `getDuration(int64&)` | `GST_QUERY_DURATION` | **no** (app-owned) | not a shared fact |
| Sync | `getSync(bool&)` | `sync` (audio, vendor) | yes (contract) | conform-when-supported |
| Stream-sync-mode | `getStreamSyncMode(int&)` | `stream-sync-mode` (audio, vendor) | yes (contract) | conform-when-supported |
| Immediate-output | `getImmediateOutput(id,bool&)` | `immediate-output` (video, vendor) | yes (contract) | conform-when-supported |
| Buffering limit | `getBufferingLimit(uint&)` | `limit-buffering-ms` (audio, vendor) | yes (contract) | conform-when-supported |
| Use-buffering | `getUseBuffering(bool&)` | `use-buffering` (audio) | no (per-session) | per-surface rows |
| Frame stats | `getStats(id,rendered,dropped)` | `stats` (base, R `GstStructure`) | no (per-session) | per-surface rows |
| Text-track id | `getTextTrackIdentifier(str&)` | `text-track-identifier` (subtitle) | no (per-session) | per-surface rows |

## Surface B only — no Surface A read-back

Correctly single-surface (native `path` rows), no consistency row applies:

- **Capabilities:** `isMimeTypeSupported`, `getSupportedProperties` (as a query
  — its *result* is cross-surface, see CONSIST-005 below).
- **DRM capabilities:** `getSupportedKeySystems`, `supportsKeySystem`,
  `getSupportedKeySystemVersion`, `isServerCertificateSupported`.
- **DRM session state:** `containsKey`, `getDrmStoreHash`, `getKeyStoreHash`,
  `getLdlSessionsLimit`, `getLastDrmError`, `getDrmTime`, `getCdmKeySessionId`,
  `getMetricSystemData`.
- **App lifecycle:** `IControl` `ApplicationState` (register-time out-param).
- **Web-audio buffer/device:** `getBufferAvailable`, `getBufferDelay`,
  `getDeviceInfo`. (`IWebAudioPlayer::getVolume` *does* overlap Surface A's
  `rialtowebaudiosink volume` — same volume contract as the row above.)

The sinks expose no key-system, DRM, app-state, or web-audio-buffer surface, so
these are Surface B's alone.

## Surface A only — no Surface B getter

Correctly single-surface (mse `path` rows), no consistency row applies:

- **Video window / decode bounds:** `rectangle`, `show-video-window`,
  `max-video-width`/`-height` (+ deprecated `maxVideoWidth`/`maxVideoHeight`),
  `frame-step-on-preroll`. Surface B's `setVideoWindow` is write-only — no
  read-back — so no agreement can be asserted.
- **Element read-backs with no native getter:** `video_pts` (video, R),
  `fade-volume` (audio, R).
- **GStreamer element concerns:** `stats`-adjacent `last-sample` /
  `enable-last-sample`, `async`, `has-drm`, `single-path-stream`,
  `streams-number`, `window-id`, `ts-offset`.
- **Write-only tuning (no read-back either surface):** `gap`, `low-latency`,
  `sync-off`, `audio-fade`, `syncmode-streaming`.

## The common interface is what is tested

Migrating an application onto Rialto means it stops driving its own (often
platform-specific) sink properties and relies on the property set Rialto
guarantees on **every** target — the *common interface*. That is the portability
the transformation buys, and the compromise it asks of the app: bespoke,
platform-specific knobs are given up for a stable common surface.

So the external-interface contract splits in two:

- **Common interface** — properties present on every Rialto target (volume, mute,
  is-master, codec caps, the unconditional sink properties). Tested
  **unconditionally**; absence is a failure. This is the surface a portable app
  may depend on.
- **Platform-specific extensions** — properties present only where the backend
  supports them (the vendor-gated set: `sync`, `stream-sync-mode`,
  `immediate-output`, …). Tested **when present**; never required, never a
  portability dependency.

The boundary between the two must be **defined and stable** for the guarantee to
mean anything. Finding 2 and [IDG-008](interface-definition-gaps.md) are exactly
where that boundary is currently undefined; [IDG-007](interface-definition-gaps.md)
is the same theme for video-placement properties that have no common-interface
equivalent at all.

## Findings

### 1. Mute overlap is uncovered → add RC-CORE-CONSIST-004

Mute is a genuine both-surfaces backend fact: Surface B `getMute`/`setMute`,
Surface A `mute` on both the audio and subtitle sinks (boolean, default
`FALSE`). It is currently unverified cross-surface. It is assertable exactly as
volume is — the **session-independent contract**: the sink's `mute` `GParamSpec`
is boolean with default `FALSE`, agreeing with the native mute default that the
native data-path row round-trips. Add CONSIST-004 as the mute analogue of
CONSIST-003.

### 2. A sink's optional-property set is a subset of the native answer → add RC-CORE-CONSIST-005

Each `rialtomse*sink` installs a vendor-gated property only if its class-init
`getSupportedProperties(type, names)` query returned that property's name — the
sink calls the native capabilities API and installs the returned subset
([audio](../framework/rialto-gstreamer/source/RialtoGStreamerMSEAudioSink.cpp),
[video](../framework/rialto-gstreamer/source/RialtoGStreamerMSEVideoSink.cpp)).
This is session-independent (both are class-init facts), so it is assertable
cross-surface. The gated names:

- **Audio:** `low-latency`, `sync`, `sync-off`, `stream-sync-mode`,
  `limit-buffering-ms`, `audio-fade`, `fade-volume`.
- **Video:** `immediate-output`, `syncmode-streaming`, `show-video-window`.

The relationship is **subset, not equality**. Native `getSupportedProperties`
is a live registry scan across *all* platform elements of the media type
([GstCapabilities.cpp](../framework/rialto/media/server/gstplayer/source/GstCapabilities.cpp)),
so it also reports properties owned only by other elements — e.g. `sync`, carried
by core `GstBaseSink` audio sinks — that the `GST_TYPE_ELEMENT`-derived rialto
sink does not expose, and its answer grows as plugins register over the process
lifetime ([IDG-008](interface-definition-gaps.md)). CONSIST-005 therefore asserts
the invariant that holds: every gated property a sink **installs** is native-
supported (a sink must not expose a knob the platform disowns); native-only names
are expected. This closes the vendor-gated overlap (sync, stream-sync-mode,
immediate-output, buffering-limit …) in one session-independent assertion rather
than a fragile per-property live case each.

The reverse direction — that native and sink **agree** on the optional-property
set — is what a defined, stable common interface would make testable, and is
recorded as a gap ([IDG-008](interface-definition-gaps.md)): while the query
stays a registry scan, the common-vs-extension boundary is undefined and `sync`
sits ambiguously across it.

### 3. Position is NOT cross-surface-assertable — per-session, by construction

Both surfaces report position (native `getPosition`, sink `GST_QUERY_POSITION`),
but only from an active session, and the two clients cannot share one. There is
no session-independent position contract to assert. Position stays covered by
its per-surface rows (native data-path + mse), not by a CONSIST row. This is the
answer to "do position reads align?": the suite cannot assert it across
independent sessions, and asserting it would require driving one identical stream
into each surface — an EXTENDED/Phase-2 shape, not a CORE consistency fact.

### 4. Duration has NO shared backend fact — divergence by design

Native `getDuration` is `gst_element_query_duration(GST_FORMAT_TIME)`, which
returns **false** for the stream-type appsrc backing every MSE source
([IDG-003](interface-definition-gaps.md)), and `notifyDuration` is never emitted
([IDG-002](interface-definition-gaps.md)) — duration is **app-owned**. There is
therefore no backend duration fact for the two surfaces to agree on: Surface B
declines to answer, and Surface A's `GST_QUERY_DURATION` is answered from
app-supplied state the backend never owns. So the handover's open question — "do
duration reads align cross-surface?" — resolves to **there is nothing to align**;
this is recorded here (and via IDG-002/003), not chased as a CONSIST row.

### 5. Web-audio volume already overlaps

`IWebAudioPlayer::getVolume` (Surface B) and `rialtowebaudiosink volume`
(Surface A) share the volume contract (double, default `1.0`, `[0.0, 1.0]`) —
the same contract CONSIST-003 asserts for the MSE audio path. No separate row is
needed; the volume contract is one fact across all three carriers.

## Disposition summary

| Overlap fact | Disposition |
|---|---|
| codec caps, is-master, volume | covered — CONSIST-001/002/003 |
| mute | **add CONSIST-004** (contract) |
| optional-property set (10 names) | **add CONSIST-005** (session-independent) |
| position | per-surface rows — not cross-surface-assertable |
| duration | no shared fact — IDG-002/003 |
| use-buffering, stats, text-track-id | per-surface rows (per-session state) |
| vendor-gated props (sync, …) | folded into CONSIST-005; contracts conform-when-supported |
