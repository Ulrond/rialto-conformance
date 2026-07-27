<!--
Copyright 2026 RDK Management
SPDX-License-Identifier: Apache-2.0
-->

# Common-interface review — the tested property boundary

The suite's purpose is a **common, platform-independent** property interface: an
application migrated onto Rialto gives up its own (often platform-specific) sink
knobs and relies on the set Rialto guarantees on *every* target. That guarantee
only means something if the **common-vs-platform-specific boundary is defined and
correct**. This note states the boundary the suite tests today, cross-checks it
against a code-derived survey of the real player/sink property surface, and
records where "common" is a weaker claim than it appears.

Cited by `RC-` / `IDG-` ids only. Pairs with
[cross-surface-fact-inventory.md](cross-surface-fact-inventory.md) ("The common
interface is what is tested") and [IDG-007](interface-definition-gaps.md) /
[IDG-008](interface-definition-gaps.md).

## The de-facto boundary the suite tests (target: rialto-gstreamer v0.22.0)

Each `rialtomse*sink` installs some `GParamSpec`s **unconditionally** and installs
the rest only when a class-init `getSupportedProperties(mediaType, names)` query
returns the name. That install-time split *is* the boundary the suite mirrors, and
it was verified line-for-line against the pinned sink source
([audio](../framework/rialto-gstreamer/source/RialtoGStreamerMSEAudioSink.cpp),
[video](../framework/rialto-gstreamer/source/RialtoGStreamerMSEVideoSink.cpp)).

### Common set — tested unconditionally (absence = FAILURE)

| Interface | Properties | Case |
|---|---|---|
| Base sink | `single-path-stream`, `streams-number`, `has-drm`, `stats`, `enable-last-sample`, `last-sample` | RC-CORE-MSEPROP-001 |
| Base sink (signals) | `buffer-underflow-callback`, `first-video-frame-callback` | RC-CORE-MSEPROP-002 |
| Audio sink | `volume`, `mute`, `gap`, `use-buffering`, `async`, `web-audio` | RC-CORE-MSEPROP-003 |
| Video sink | `rectangle`, `max-video-width`, `max-video-height`, `frame-step-on-preroll`, `is-master`, `video_pts` | RC-CORE-MSEPROP-006 |
| Video sink | `show-video-window`, `report-decode-errors`, `queued-frames` | RC-CORE-MSEPROP-008 / RC-CORE-MSEPROP-011 |
| Subtitle sink | `mute`, `text-track-identifier`, `window-id`, `async` | RC-CORE-MSEPROP-009 |

### Platform-specific extensions — tested when-present (never required)

| Interface | Properties | Case |
|---|---|---|
| Audio sink | `low-latency`, `sync`, `sync-off`, `stream-sync-mode`, `audio-fade`, `fade-volume`, `limit-buffering-ms` | RC-CORE-MSEPROP-005 |
| Video sink | `immediate-output`, `syncmode-streaming` | RC-CORE-MSEPROP-008 |

The cross-surface subset guard (RC-CORE-CONSIST-005) holds the invariant that a
sink's installed extension set is a subset of the native `getSupportedProperties`
answer.

## Cross-check verdict

The split above **faithfully mirrors the pinned sink** — the unconditional set is
exactly the properties installed outside the `getSupportedProperties` guard, and
the gated set is exactly the properties inside it, for both audio and video.

Because the sink is a **single client-side element** (one binary driving the
Rialto Server over IPC — the SoC variation is absorbed server-side, not in the
element), every unconditionally-installed `GParamSpec` is present on *every* target
running that release. So at the level the common cases actually assert —
`GParamSpec` existence, type, default, flags, range — **no common-set member can be
absent on a conformant target**. The common *property* surface is uniform by
construction.

That is also the limit of the guarantee. The platform-specificity a code-derived
survey of the real player/sink surface exposes is **not** in property *presence*;
it is in two places the existence-level split cannot see.

### Finding A — the boundary is release-unstable (`show-video-window`)

`show-video-window` has **crossed the common↔extension boundary between releases**,
in the targeted direction, within the surface this suite gates:

| Release | Installation | Classification |
|---|---|---|
| v0.20.1 | inside the `getSupportedProperties` guard | when-present extension |
| v0.22.0 | unconditional, outside the guard | common member |

At v0.22.0 the guard covers only `immediate-output` and `syncmode-streaming`;
`show-video-window` is installed alongside `rectangle` and `frame-step-on-preroll`
in the unconditional block of `RialtoGStreamerMSEVideoSink` class-init.

This is no longer a projection from a divergent revision — it is the observed
behaviour of two consecutive targeted releases. **"Common" is defined only relative
to a release pin.** Direct evidence for [IDG-008](interface-definition-gaps.md)
(the boundary is registry-dependent and undefined).

Consequence for the suite, discharged at the v0.22.0 pin: RC-CORE-MSEPROP-008
asserts `show-video-window` **unconditionally** (absence is a FAILURE, per the
"only variable features are gated" invariant) and it is removed from the gated set
RC-CORE-CONSIST-005 checks against native `getSupportedProperties`. A when-present
assertion would have silently under-tested it — passing whether or not the platform
installed a property the release now requires.

### Finding B — unconditional `GParamSpec` ≠ portable behaviour

Two members of the common **video** set map to genuinely platform-variable
*capabilities*, yet are installed unconditionally by the sink:

- **`frame-step-on-preroll`** — the underlying step-while-paused capability is
  present on some real video backends and **absent on at least one other**. The
  sink installs the property on every target regardless.
- **`max-video-width` / `max-video-height`** — a decode-resolution ceiling
  (default UHD `3840`×`2160`), inherently bounded by what a platform can decode,
  and exercised by only a **single application** in the surveyed set. The sink
  installs it with the same UHD default on every target, including targets that
  cannot reach that resolution.

For both, the `GParamSpec` **presence** is common (uniform binary) but the
**honoured behaviour** is platform-specific. The suite asserts only
existence/type/default for these (L1 introspection), so it does **not**
over-assert — but the boundary it inherits from the sink treats a platform-variable
capability as common. This is the [IDG-007](interface-definition-gaps.md) /
[IDG-008](interface-definition-gaps.md) theme: the sink's unconditional-install
decision is not a reliable definition of the common (portable) *behavioural*
contract.

## Disposition

No property currently tested as common is removed from the unconditional existence
assertions — those are correct for the v0.22.0 pin and safe, because `GParamSpec`
presence is uniform by construction.

What is **invalid** is the implicit assumption that *"the sink installs it
unconditionally"* defines the common (portable) contract. Concretely:

| Property | Common at | Platform-specific at | Action |
|---|---|---|---|
| `show-video-window` | unconditional at v0.22.0 | membership moved across releases | IDG-008 evidence; MSEPROP-008 asserts it unconditionally, CONSIST-005 no longer gates it |
| `frame-step-on-preroll` | property presence | honoured behaviour | any future *behavioural* (L4) assertion MUST be when-present, not unconditional |
| `max-video-width` / `-height` | property presence | honoured ceiling | as above; existence/default assertion unchanged |

The standing consequence for the suite: **behavioural** conformance on a common
property is only portable where the property is *both* unconditionally present *and*
the behaviour is guaranteed on every target. Presence-in-the-element is necessary,
not sufficient. Until the common set is defined authoritatively upstream
([IDG-008](interface-definition-gaps.md) question (a)), the suite defines "common"
operationally as *installed-unconditionally-at-the-target-pin*, tests that at the
existence level unconditionally, and keeps every platform-variable behaviour gated.
