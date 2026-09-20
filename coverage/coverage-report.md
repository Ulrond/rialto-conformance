<!--
Copyright 2026 RDK Management
SPDX-License-Identifier: Apache-2.0
-->

# What is tested, and what is not

The CORE tier against rialto **v0.24.0** + rialto-gstreamer **v0.22.0**. Every
`RC-CORE-*` requirement sits in one of three states, and this is the whole list.

| | Rows | Meaning |
|---|---|---|
| **Tested** | 123 | a case asserts it, and it runs |
| **Held** | 5 cases | the case exists and self-skips until a target declares the feature |
| **Untested** | 9 | no case yet — each waits on something the interface alone cannot provide |

A run on the Linux software platform is **121 cases → 116 pass, 5 skip, 0 fail**,
whether driven through the emulator slot or the dev loop. Case count and row
count differ because one case can carry several requirements and one requirement
can be asserted on both interfaces.

## Tested — 123 rows

| Area | Rows | What is asserted |
|---|---|---|
| **PIPE** | 30 | the whole `IMediaPipeline` surface — source attach, load, play, pause, seek, EOS, position, playback-state machine, per-source properties |
| **MSEEVENT** | 13 | the sink elements' signals and event handling |
| **MSEPROP** | 11 | every documented sink property, each at its own scope |
| **CAPS** | 9 | codec and mime reporting through the capabilities API |
| **KEYS** | 9 | the media-keys session lifecycle against a real W3C ClearKey CDM |
| **WEBAUDIO** | 8 | the web-audio player surface, including async property application |
| **KEYSCAP** | 7 | key-system support and version reporting |
| **DATA** | 10 | the transfer protocol — need-data / have-data, real elementary streams to PLAYING, and its failure clauses: a full buffer, an unexpected call, an unanswered request, a decode fault |
| **MSECAPS** | 6 | sink pad-template caps and negotiation |
| **MSESTATE** | 6 | sink state transitions |
| **CONSIST** | 5 | the two interfaces agree wherever they expose the same fact |
| **CONTROL** | 4 | the control plane |
| **LOG** | 4 | log-level control |
| **MSE** | 3 | element registration under the documented names |
| **FACTORY** | 2 | every public factory constructs |
| **CODEC** | 2 | H.264 and AAC advertised and played end to end |

Real bitstreams, not mocks: the data path is driven with genuine ADTS AAC and
H.264 synthesised in-process by GStreamer's own encoders, which the server's
decoder consumes.

## Held — 5 cases, gated on a platform feature

Written, and skipped until a target's capability profile declares the feature.
Absence of the feature is a skip; absence of the **required** surface is a
failure.

| Case | Runs when the target has |
|---|---|
| `L1CapabilitiesTests.Av1MimeTypeSupportedWhenDeclared` | AV1 |
| `L1KeysCapabilitiesTests.WidevineSupportedWhenDeclared` | Widevine |
| `L1KeysCapabilitiesTests.PlayReadySupportedWhenDeclared` | PlayReady |
| `L1WebAudioTests.StateMachineNotifiesTransitions` | web-audio state notifications |
| `L3MemoryUsageTests.CapabilitiesLifecycleChurnDoesNotLeak` | a measurable memory surface |

## Untested — 9 rows

| Waits on | Rows | Why the interface alone cannot reach it |
|---|---|---|
| **A vendor CDM** | KEYS-004, KEYS-007, KEYS-008, KEYS-010 | ClearKey has no DRM header and no license renewal, and accepts `update` in any constructed state — the assertions are about behaviour a licensed CDM has and it does not |
| **A sanitizer, not a server** | DATA-008 | a segment's buffer must stay valid until `haveData` completes — an obligation on the client, so nothing the server reports can prove or disprove it |
| **An upstream answer** | DATA-011 | the server never calls `notifyDuration` at this release (IDG-002) — no client action makes a callback fire that is not emitted |
| **A video-rendering target** | PIPE-016 | the immediate-output flag lives on the video sink, which needs a native video feed |
| **A subtitle-capable target** | PIPE-020, PIPE-027 | the software platform stubs the text-track service |

Seven of the nine fall to a single target with a vendor CDM, video and subtitles,
which is what [on-target-promotion-plan.md](on-target-promotion-plan.md)
sequences. The four fault clauses that were here — a full shared buffer, an
unexpected call, an unanswered request and a decode fault — moved into *Tested*:
a feed that deliberately misbehaves reaches them from the public API, so they
needed no injection surface after all. Where the server's announcement is
platform-dependent (a cancel on flush, a decoder reporting a dropped frame) the
case asserts the clause that always holds and records the rest.

## Two gaps with no row

- **`com.apple.fps`** — no case and no target; needs FairPlay provisioning.
- **IDG-008** — `getSupportedProperties` is a registry scan, so the boundary
  between the common property set and a platform's extensions is undefined.
  Filed as [rdkcentral/rialto#557](https://github.com/rdkcentral/rialto/issues/557).
  The v0.24.0 bump made it concrete: `show-video-window` left the guard between
  two consecutive releases, moving the boundary under a suite that had asserted
  it. See [interface-definition-gaps.md](interface-definition-gaps.md).

## Where this comes from

[matrix.yaml](matrix.yaml) is the authority — every row carries its requirement
ids, the interface it is asserted on, the case that asserts it and its status.
This report is that file read back at a level a reader can hold in their head.
