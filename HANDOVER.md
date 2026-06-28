<!--
Copyright 2026 RDK Management
SPDX-License-Identifier: Apache-2.0
-->

# HANDOVER — rialto-conformance

**Self-contained session-starter.** Read this to pick the project back up. It is
the bridge between sessions: current state, the decisions that must not regress,
and exactly where to start next.

## Read first (in order)

1. This file (state + invariants + next actions).
2. [README.md](README.md) — what the suite is and how it is shaped.
3. The requirements doc (the source of truth, a self-contained session-starter
   itself): `../rialto_test_suite/docs/EXTERNAL-INTERFACE-CONFORMANCE-REQUIREMENTS.md`.
4. The three RDK harness AI briefs (branch `feature/ai-brief`): ut-core,
   python_raft, and the ut-control brief they reference — they define the tooling.

## Where things are

| What | Location |
|---|---|
| **This repo (the suite)** | `/home/gew04/git/fast/sky/rialto-conformance` — local git, **not pushed** |
| **Target GitHub repo** | `Ulrond/rialto-conformance` (public) — **not created yet** (held for review) |
| **Requirements doc** | `../rialto_test_suite/docs/EXTERNAL-INTERFACE-CONFORMANCE-REQUIREMENTS.md` (not under git) |
| **Rialto API reference (local)** | `../rialto_test_suite/external/rialto-develop` (rdkcentral/rialto @ v0.22.2) |
| **Tracking issues** | `Ulrond/rialto` #7 (harness), #2 (native session admission), #9 (in-tree suite re-cut) |

`gh` is authed as **Ulrond**. Local commits so far: `8c13532` (scaffold),
`fbc8653` (install model + raft-shaped gate).

## Status — Phase 0 (ut-core bring-up + packaging): DONE, local-only

The harness is stood up and the §7 layout is in place, committed locally, **not
pushed** pending review. A green end-to-end build needs a host/target with an
installed Rialto + GStreamer (absent on this dev box by design — the suite links
them on the target, never builds them). Shell/Python/YAML pass syntax/parse checks.

## Install / build / run

```bash
./install.sh        # clone+pin framework deps (framework.lock) into framework/  (gitignored)
./build.sh          # build VARIANT=CPP -> build/bin/rialto_conformance
# on a target with Rialto installed (ut-raft does this for you):
./rialto_conformance -a -p deviceConfig.yml
python raft/suites/test_rialto_conformance.py --config raft/rack_config.yml \
       --rack rack1 --slotName reference-target
```

## Decisions baked in (DO NOT regress)

1. **Frameworks are installed, never committed.** `install.sh` clones every dep
   in `framework.lock` at a fixed ref into the gitignored `framework/`: ut-core
   5.1.0, python_raft 1.8.2, ut-raft 2.1.2, **rialto v0.22.2**, rialto-gstreamer
   v0.20.1 (ut-control 2.1.0 + GoogleTest 1.15.2 come from ut-core's build.sh).
   The Rialto sources are the **API reference the cases compile against**; the
   suite never builds Rialto and links only the installed `libRialtoClient`.
2. **Platform-agnostic; gate on platform FEATURES, not SoC.** One binary, same
   cases everywhere. There are **no per-SoC profiles**. A SoC may be capable of
   something its platform does not support, and two platforms on one SoC can
   differ — so the gate records what the *platform* supports.
3. **Only variable features are gated.** The standard required surface (MSE
   audio/video/text sinks, core native interfaces, baseline H.264 + AAC) is
   tested **unconditionally** — absence is a FAILURE, not a skip. A toggle in the
   profile means "this genuinely varies between platforms".
4. **deviceConfig follows python_raft shape.** `deviceConfig: → cpe1: →
   platform:` with the capability gate nested at `rialto:`. The one file is both
   the raft device config and the on-target `-p` KVP profile. Cases read
   `UT_KVP_PROFILE_GET_BOOL("deviceConfig/cpe1/rialto/<key>")` — the root is the
   single constant `kCapabilityRoot` in `include/conformance/CapabilityGate.h`.
5. **No vendor-specific PlayReady variant.** There is just standard PlayReady
   (`com.microsoft.playready`) — no separate key system. FairPlay
   (`com.apple.fps`) is a real Rialto gap, recorded in `coverage/matrix.yaml`.
6. **ABI-versioned, additive.** Cases declare the ABI they entered at
   (`CONFORMANCE_REQUIRE_ABI(n)`); an older certified backend is never failed by a
   newer additive case. Current `kPlatformBackendAbiVersion = 5`.

## What exists now

- `install.sh` + `framework.lock`; `build.sh` + `Makefile` (downstream ut-core,
  links only `libRialtoClient` + GStreamer, headers from pinned `framework/rialto`).
- `include/conformance/`: CapabilityGate, AbiVersion, ContentLoader, Surfaces.
- `src/main.cpp` + `src/common/Surfaces.cpp`; **L1 smoke cases** proving the link
  surface — native `IMediaPipelineCapabilities` (`src/L1_function/native/`) and
  MSE `rialtomse*sink` registration (`src/L1_function/mse/`). L2–L4 trees
  scaffolded (empty).
- `profiles/` schema + example; `raft/` rack+device config + the `RAFTUnitTestCase`
  adjudicator; `coverage/matrix.yaml` seeded incl. the FairPlay gap;
  `assets/manifest.yaml`; `packaging/package.sh`.

## Start here next

1. **Decide: create + push `Ulrond/rialto-conformance`** (held for review). Until
   then nothing leaves this box.
2. **Climb the levels L1 → L4** (README table). Author the full case set first,
   then run on whatever target is available; applicability is the capability gate,
   never platform-specific test code. Each case traces to a `coverage/matrix.yaml`
   row (req ref + expected + ABI).
3. **Requirement-catalog ingestion** (gates L4). Mount the private requirements
   feed at `coverage/requirements/` (gitignored — see its README), add `RC-*`
   crosswalk entries, and grow the matrix. The public suite cites only `RC-*`
   ids; partner provenance stays in the private feed, never in this repo. Can run
   parallel to L1–L2.
4. **Real assets.** `assets/manifest.yaml` has placeholder `REPLACE_ME` URLs +
   zeroed checksums — point at free-to-use clips and implement `ContentLoader`.

## Open items / to verify on real hardware

- **ut-raft API touchpoints.** The adjudicator uses the documented surface
  (`self.dut.session`, `self.dut.config`) plus a `getattr`-guarded file transfer
  with an `scp` fallback. Verify against installed ut-raft 2.1.2 on a real rack
  and tighten `_copy_to/from_target` in `raft/suites/test_rialto_conformance.py`.
- **AV1 mime string.** The gated L1 example asserts `video/x-av1`; confirm the
  exact mime Rialto advertises and align the manifest/matrix.
- **deviceConfig per target.** `raft/device_config.yml` is an example instance;
  each real target gets its own copy of `profiles/deviceConfig.example.yaml` with
  the `rialto:` toggles set to the features that target's *platform* supports.
