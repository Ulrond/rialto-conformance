<!--
Copyright 2026 RDK Management
SPDX-License-Identifier: Apache-2.0
-->

# Interface-completeness matrix

`matrix.yaml` is **requirements-derived**: it maps each `RC-CORE-*` id to the case
that proves it, answering *"is every requirement we wrote tested?"*. This record is
the complementary lens — **interface-derived**: it enumerates the *entire* surface
of both interfaces from the source of truth and marks each element tested or not,
answering *"is every field of the interface tested?"*. A method or property for
which no `RC-*` id was ever written is invisible in `matrix.yaml`; here it shows as
`UNTESTED`.

The two interfaces (§2):

- **Surface A — MSE sink** — the `rialtomse{audio,video,subtitle}sink` GObject
  properties, signals, caps, and element registration. Source of truth:
  `framework/rialto-gstreamer/source/`.
- **Surface B — native client API** — the public C++ methods in
  `framework/rialto/media/public/include/*.h`.

**Status:**

- `COVERED` — a case invokes the element and asserts on it.
- `GATED` — a case executes but the assertion is conditional on platform capability
  (vendor-optional property, or a best-effort vendor signal): conform-when-present.
- `UNTESTED` — no executing case touches it. For an element `matrix.yaml` marks
  `planned`, the deferral reason is given.

---

## Surface A — MSE sink

Every installed `GParamSpec` maps 1:1 to an `RC-CORE-MSE*` case: the L1
`PropertyTests` / `CapsTests` / `SinkRegistrationTests` mirror the full property
enumeration. **34 properties, 0 untested.**

### Element registration & metadata (all three sinks)

| Surface | Status | Case / RC id |
|---|---|---|
| `rialtomse{audio,video}sink` register + instantiate | COVERED | L1SinkRegistrationTests.{Audio,Video}SinkRegistered — MSE-001 |
| `rialtomsesubtitlesink` registers | COVERED | RegistrationIsRankGated + SinkFactoryKlassMetadata — MSE-001/003 |
| Rank-gating (`RIALTO_SINKS_RANK`; rank 0 ⇒ none) | COVERED | RegistrationIsRankGated — MSE-002 |
| Factory klass metadata (audio/video/subtitle) | COVERED | SinkFactoryKlassMetadata — MSE-003 |
| Audio sink implements `GstStreamVolume` | COVERED | AudioSinkImplementsStreamVolume — MSE-003 |
| Single ALWAYS `sink` pad template (each) | COVERED | SinkPadTemplateIsSingleAlwaysSink — MSECAPS-001 |
| CAPS-event field parsing (codec_data, alignment, stream-format, DV, raw) | COVERED (baseline leg) | MSECAPS-006 (AVC data-path, L4) |

### rialtomseaudiosink (own properties)

| Property | Status | Case / RC id | Note |
|---|---|---|---|
| `volume`, `mute`, `gap`, `use-buffering`, `async` | COVERED | AudioSinkProperties — MSEPROP-003 | mandatory |
| `web-audio` | COVERED (functional, NULL-state-gated set/get) | AudioSinkProperties + AudioSinkWebAudioSettableOnlyInNull — MSEPROP-003/004 | mandatory |
| `low-latency`, `sync`, `sync-off`, `stream-sync-mode`, `audio-fade`, `fade-volume`, `limit-buffering-ms` | GATED | AudioSinkConditionalProperties — MSEPROP-005 | vendor-gated |
| Sink-pad caps (mpeg v1/2/4, x-ac3, x-eac3, x-opus, b-wav, x-flac, x-raw) | COVERED | AudioMimeToCapsMappingCorrect + AdvertisedCapsAreDocumentedSubset — MSECAPS-002/003 | |

**13 properties: 6 mandatory COVERED, 7 vendor-GATED, 0 UNTESTED.**

### rialtomsevideosink (own properties)

| Property | Status | Case / RC id | Note |
|---|---|---|---|
| `rectangle`, `max-video-width`, `max-video-height`, `frame-step-on-preroll`, `is-master`, `video_pts` | COVERED | VideoSinkProperties — MSEPROP-006 | mandatory |
| `maxVideoWidth`, `maxVideoHeight` (deprecated aliases) | COVERED (functional round-trip) | VideoSinkDeprecatedAliases — MSEPROP-007 | mandatory |
| `immediate-output`, `syncmode-streaming`, `show-video-window` | GATED | VideoSinkConditionalProperties — MSEPROP-008 | vendor-gated |
| Sink-pad caps (x-h264, x-h265, x-av1, x-vp9) | COVERED | VideoMimeToCapsMappingCorrect + subset — MSECAPS-002/004 | must advertise x-h264 |

**11 properties: 8 mandatory COVERED, 3 vendor-GATED, 0 UNTESTED.** MSEPROP-008
asserts the source-installed defaults for the gated leg: `immediate-output` (R/W,
default TRUE), `syncmode-streaming` (W-only, default FALSE), `show-video-window`
(W-only, default TRUE).

### rialtomsesubtitlesink (own properties)

| Property | Status | Case / RC id |
|---|---|---|
| `mute`, `text-track-identifier`, `window-id`, `async` | COVERED | SubtitleSinkProperties — MSEPROP-009 |
| Sink-pad caps (text/vtt, x-subtitle-vtt, ttml+xml, cea-608/708) | COVERED | SubtitleMimeToCapsMappingCorrect + subset — MSECAPS-002/005 |

**4 properties: 4 mandatory COVERED, 0 UNTESTED.**

### RialtoMSEBaseSink (inherited by all three)

| Property / surface | Status | Case / RC id |
|---|---|---|
| `single-path-stream`, `streams-number`, `has-drm` | COVERED | BaseSinkProperties + ReadablePropertiesFallbackDefaults — MSEPROP-001/010 |
| `stats`, `enable-last-sample`, `last-sample` | COVERED (spec only) | BaseSinkProperties — MSEPROP-001 |
| Signals `buffer-underflow-callback`, `first-video-frame-callback` | COVERED | BaseSinkSignals — MSEPROP-002 |
| Readable-fallback-default contract | COVERED | ReadablePropertiesFallbackDefaults — MSEPROP-010 |

**6 properties + 2 signals, all COVERED.** Read-only *value* semantics of `stats`,
`last-sample`, `video_pts`, `is-master`, `fade-volume` are asserted at the
GParamSpec level only, not functionally retrieved.

---

## Surface B — native client API

**115 public methods: 92 COVERED, 8 GATED, 15 UNTESTED.**

### Factory classes — 14/14 COVERED

Every `createFactory()` (RC-CORE-FACTORY-001 / L1FactoryTests.EveryPublicFactoryIsNonNull —
`IClientLogControlFactory` via L1ClientLogControlTests) and every product
constructor (`createControl`, `createMediaPipeline`, `createMediaPipelineCapabilities`,
`createMediaKeys` [FACTORY-002], `getMediaKeysCapabilities`, `createWebAudioPlayer`
[WEBAUDIO-001], `createClientLogControl`).

### IControl / IControlClient / IClientLogControl / IClientLogHandler — all COVERED

| Method | Status | Case / RC id |
|---|---|---|
| IControl::registerClient | COVERED | CONTROL-001 / L1ControlTests.RegisterClientReturnsTrueAndWritesState |
| IControl::~IControl (unregister-on-destroy) | COVERED | CONTROL-003 / L1ControlTests.ClientReleasedOnControlDestruction |
| IControlClient::notifyApplicationState | COVERED | CONTROL-002 / L1ControlTests.RegisteredClient{LearnsRunningState,IsNotifiedOfStateTransition} |
| IClientLogControl::registerLogHandler | COVERED | LOG-002/003/004 / L1ClientLogControlTests.* |
| IClientLogHandler::log (all 5 fields) | COVERED | L1ClientLogControlTests.HandlerReceivesPopulatedRecords |

### IMediaPipelineCapabilities — 4/4 COVERED

`getSupportedMimeTypes`, `isMimeTypeSupported`, `getSupportedProperties`,
`isVideoMaster` — L1CapabilitiesTests.* (CAPS-001..004).

### IMediaKeysCapabilities — 4/4 COVERED

`getSupportedKeySystems`, `supportsKeySystem`, `getSupportedKeySystemVersion`,
`isServerCertificateSupported` — L1KeysCapabilitiesTests.* (KEYSCAP-001..004).

### IMediaKeys — 17/19, **2 UNTESTED**

| Method | Status | Case / RC id / reason |
|---|---|---|
| createKeySession | COVERED | KEYS-001 / L1KeysSessionTests.CreateKeySessionWritesValidId |
| generateRequest | COVERED | KEYS-002 / GenerateRequestDeliversLicenseRequest |
| updateSession, containsKey | COVERED | KEYS-003/004 / UpdateSessionSurfacesUsableKeyStatus |
| closeKeySession, removeKeySession, releaseKeySession | COVERED | KEYS-005 / SessionTeardownReturnsOk |
| getDrmTime, getCdmKeySessionId, getDrmStoreHash, getKeyStoreHash, getLdlSessionsLimit, getLastDrmError, getMetricSystemData | COVERED | KEYS-011 / StoreAndMaintenanceQueriesAnswer |
| deleteDrmStore, deleteKeyStore | COVERED | KEYS-011 / StoreAndMaintenanceQueriesAnswer |
| selectKeyId | COVERED | KEYS-013 / SelectKeyIdAnswersForValidSession (defined status; ClearKey is single-key) |
| loadSession | UNTESTED (planned) | wrong-state/unsupported-type semantics are CDM-specific — needs a vendor CDM |
| setDrmHeader | UNTESTED (planned) | PlayReady-specific — not exercisable on ClearKey |

### IMediaKeysClient (callbacks) — 2/3

`onLicenseRequest` (KEYS-002), `onKeyStatusesChanged` (KEYS-003) COVERED;
`onLicenseRenewal` UNTESTED (planned — renewal doesn't exist on ClearKey; needs a
vendor CDM).

### IMediaPipeline — 31/40, 4 GATED, 5 UNTESTED

COVERED: `getClient` (PIPELINE-002); `load`/`attachSource`/`removeSource`/`allSourcesAttached`
(L2PipelineLifecycleTests, PIPE-003/004/005/006); `play`/`pause`/`stop`/`setPlaybackRate`/`setPosition`
(L4PipelineTransportDataPathTests, PIPE-007..011); `getPosition`/`getDuration`/`getStats`
(L4PlaybackDataPathTests); `haveData`/`addSegment` (L4DataProtocolTests, DATA-001/002 — OK path);
`renderFrame`/`setVideoWindow`/`setVolume`/`getVolume`/`setMute`/`getMute`/`flush`/`setSourcePosition`/`processAudioGap`/`setLowLatency`/`setSync`/`getSync`/`setSyncOff`/`setUseBuffering`/`getUseBuffering`/`switchSource`
(L4PipelineAuxDataPathTests).

| Method | Status | Reason |
|---|---|---|
| setBufferingLimit / getBufferingLimit | GATED | BufferingLimitConformsWhenSupported — read-back only when decoder supports it |
| setStreamSyncMode / getStreamSyncMode | GATED | StreamSyncModeConformsWhenSupported |
| setImmediateOutput / getImmediateOutput | UNTESTED (planned) | property lives on the video sink — needs a native VIDEO feed (data path is audio-only) |
| setTextTrackIdentifier / getTextTrackIdentifier | UNTESTED (planned) | needs a SUBTITLE source + platform text-track service |
| setSubtitleOffset | UNTESTED (planned) | needs a SUBTITLE source |
| addSegment NO_SPACE/ERROR return codes | UNTESTED (planned) | OK path covered; fault paths need injection |

### IMediaPipelineClient (callbacks) — 4/15, 4 GATED, 7 UNTESTED

| Callback | Status | Reason |
|---|---|---|
| notifyPlaybackState | COVERED | recorded + asserted across PlayPauseStop / EOS / network-state cases |
| notifyNetworkState | COVERED | DATA-009 / CleanRunNetworkStateVocabulary |
| notifyNeedMediaData | COVERED | DATA-001/002 / NeedDataProtocolIsWellFormed |
| notifySourceFlushed | COVERED | FlushEmitsSourceFlushed |
| notifyBufferUnderflow, notifyFirstFrameReceived | GATED | DATA-012 / StarvationIsToleratedAndEventsObserved — best-effort vendor signals |
| notifyQos, notifyPosition | GATED | DATA-012 / StarvationIsToleratedAndEventsObserved — harness records both; delivery observed, a delivered position asserted non-negative |
| notifyNativeSize, notifyVideoData, notifyAudioData, notifyPlaybackInfo | UNTESTED | empty harness body |
| notifyDuration | UNTESTED (planned) | server never emits it in v0.22.3 (IDG-002) |
| notifyCancelNeedMediaData | UNTESTED (planned) | server-initiated, not provokable from the client API |
| notifyPlaybackError | UNTESTED (planned) | needs non-fatal error injection |

### IWebAudioPlayer / IWebAudioPlayerClient — 11/11

COVERED: `play`/`pause`/`setEos` (WEBAUDIO-002), `getBufferAvailable`/`writeBuffer`
(WEBAUDIO-003), `getBufferDelay`, `getDeviceInfo`, `setVolume`/`getVolume`,
`getClient` (WEBAUDIO-008 / GetClientReturnsSuppliedClient),
`IWebAudioPlayerClient::notifyState` — L1WebAudioTests.*.

### Data value types

`MediaSourceAudio` + `MediaSegmentAudio` are constructed and fed across the native
cases. `MediaSourceVideo` is constructed (attach-only) in
L2PipelineLifecycleTests.MultiSourceAttachComposition but never fed.
`MediaSegmentVideo`, `MediaSourceVideoDolbyVision`, and `MediaSourceSubtitle` are
**never constructed** — the native video and subtitle data paths are unexercised
(audio-AAC only), which is the root cause of most of the deferred
IMediaPipeline / IMediaPipelineClient gaps above.

---

## Gaps and discrepancies

The software-provable gaps and the MSEPROP-008 default discrepancy are **closed**:
`IMediaKeys::selectKeyId` (KEYS-013), `deleteDrmStore`/`deleteKeyStore` (KEYS-011),
`IWebAudioPlayer::getClient` (WEBAUDIO-008) now have cases; `notifyQos` and
`notifyPosition` are recorded by the harness and observed (DATA-012), removing the
notifyQos false-positive; and MSEPROP-008 asserts the source-installed defaults.

### Deferred — need real hardware / vendor CDM / a video·subtitle feed (see #89)

- Vendor CDM: `loadSession`, `setDrmHeader`, `onLicenseRenewal`.
- Native video feed (`MediaSegmentVideo`): `setImmediateOutput`/`getImmediateOutput`,
  video QoS, `notifyNativeSize`/`notifyVideoData`.
- Subtitle source (`MediaSourceSubtitle`): `setTextTrackIdentifier`/`getTextTrackIdentifier`,
  `setSubtitleOffset`.
- Fault injection: `addSegment` NO_SPACE/ERROR, `notifyPlaybackError`.
- Server-model: `notifyDuration` (IDG-002), `notifyCancelNeedMediaData`.

---

## Summary

| Interface | Total | COVERED | GATED | UNTESTED |
|---|---|---|---|---|
| Surface A — MSE sink (properties) | 34 | 24 | 10 | 0 |
| Surface B — factories + IControl/log | 19 | 19 | 0 | 0 |
| Surface B — IMediaPipelineCapabilities | 4 | 4 | 0 | 0 |
| Surface B — IMediaKeysCapabilities | 4 | 4 | 0 | 0 |
| Surface B — IMediaKeys | 19 | 17 | 0 | 2 |
| Surface B — IMediaKeysClient | 3 | 2 | 0 | 1 |
| Surface B — IMediaPipeline | 40 | 31 | 4 | 5 |
| Surface B — IMediaPipelineClient | 15 | 4 | 4 | 7 |
| Surface B — IWebAudioPlayer(+Client) | 11 | 11 | 0 | 0 |

Surface A is complete on the existence axis (0 untested). Surface B's remaining
untested set is deferred on the native video/subtitle/CDM paths that the
deployed-stack run (#89) covers; the software-provable gaps are closed.
