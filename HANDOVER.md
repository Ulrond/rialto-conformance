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
| **This repo (the suite)** | `/home/gew04/git/fast/sky/rialto-conformance` — **published** at [Ulrond/rialto-conformance](https://github.com/Ulrond/rialto-conformance) (public, branch `master`) |
| **Private requirements feed** | `/home/gew04/git/fast/sky/rialto-conformance-requirements` — local git; comcast-sky push **blocked** (org requires a `DevHub-Application-ID` at repo creation). Mounts into the public checkout at `coverage/requirements/` (gitignored). |
| **Requirements doc** | `../rialto_test_suite/docs/EXTERNAL-INTERFACE-CONFORMANCE-REQUIREMENTS.md` (not under git) |
| **Rialto API reference (local)** | `../rialto_test_suite/external/rialto-develop` (rdkcentral/rialto @ v0.22.2) |
| **Tracking issues** | `Ulrond/rialto` #7 (harness), #2 (native session admission), #9 (in-tree suite re-cut) |

`gh` has two accounts: **Ulrond** (public repo; orgs sky-uk/rdkcentral/qtasks)
and **gerald-weatherup_comcast** (the only one that reaches comcast-sky). Use
per-command tokens (`gh auth token --user <acct>` → `GH_TOKEN` / token-over-HTTPS
push), never `gh auth switch`, so the right account always acts. Public history
was squashed to a single clean commit before first push (the pre-scrub commits
contained source/partner names); do not restore that old history.

## Status — Phase 0 DONE + published; Phase 1 (CORE requirement catalogue) DONE

The harness + §7 layout are in place and **published** (public repo, branch
`master`). The suite is **source-neutral**: requirements are cited by suite-owned
`RC-*` ids only — no partner/app names anywhere in the public tree (enforced; the
provenance lives only in the private feed). Phase 1 enumerated the external
interface contract into **118 `RC-CORE-*` requirements**
([coverage/rc-core-catalog.yaml](coverage/rc-core-catalog.yaml)) — the CORE
drop-in/transform-safety gate. A green end-to-end build needs a host/target with
an installed Rialto + GStreamer (absent on this dev box by design — the suite
links them on the target, never builds them). Shell/Python/YAML pass parse checks.

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
   The Rialto sources are the **API reference the cases compile against**; by
   default the build links only the *installed* `libRialtoClient` (real targets).
   **Amended:** Linux is a first-class platform — `build-rialto.sh` opt-in builds
   Rialto + rialto-gstreamer via their own `NATIVE_BUILD` (platform deps stubbed)
   into `framework/.native-install`, which the Makefile auto-discovers. So the
   suite *can* build a software Rialto on request; the default (link installed)
   is unchanged. Linux profile: `profiles/deviceConfig.linux.yaml`.
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
7. **Two tiers, orthogonal to L1–L4.** **CORE** = interface conformance
   (`coverage/rc-core-catalog.yaml`, the `RC-CORE-*` ids) — the drop-in /
   transform-safety gate, run first. **EXTENDED** = app/player conformance
   (Phase 2, fed by the private requirements feed). A requirement is
   surface-neutral: where a fact is exposed on both surfaces it is tested once per
   **path** (native + mse), plus a `path: both` **consistency** case. Tests are
   never path-agnostic — each case drives one surface as itself; no shared
   wrapper runs one body on both paths.

## What exists now

- `install.sh` + `framework.lock`; `build.sh` + `Makefile` (downstream ut-core,
  links only `libRialtoClient` + GStreamer, headers from pinned `framework/rialto`).
- `include/conformance/`: CapabilityGate, AbiVersion, ContentLoader, Surfaces.
- `src/main.cpp` + `src/common/Surfaces.cpp`; **L1 smoke cases** proving the link
  surface — native `IMediaPipelineCapabilities` (`src/L1_function/native/`) and
  MSE `rialtomse*sink` registration (`src/L1_function/mse/`). L2–L4 trees
  scaffolded (empty).
- `profiles/` schema + example; `raft/` rack+device config + the `RAFTUnitTestCase`
  adjudicator; `assets/manifest.yaml`; `packaging/package.sh`.
- **`coverage/rc-core-catalog.yaml`** — the 118 `RC-CORE-*` interface-conformance
  requirements (Phase 1). **`coverage/matrix.yaml`** — coverage view (tier +
  path + case + status) referencing those ids; the FairPlay gap sits in EXTENDED.

## Start here next

1. **Author the CORE cases** against `coverage/rc-core-catalog.yaml`, climbing
   L1 → L4 (README table). Each case declares its tier (`CONFORMANCE_CORE_TEST()`,
   `include/conformance/TierGate.h`) plus any cap/ABI gate, and traces to a
   `coverage/matrix.yaml` row (`RC-CORE-*` + path + expected + ABI);
   negative/quiet-fail reqs are first-class. Needs a target with an installed
   Rialto + GStreamer to run green. (Tier axis is wired: the gate runs alone via
   `RIALTO_CONFORMANCE_TIER=core`, orthogonal to the `-e UT_TESTS_Ln` level
   groups — ut-core owns the L1–L4 group enum, so tier is an in-test self-skip.)
2. **Phase 2 — EXTENDED / app-requirement conformance.** Create the comcast-sky
   private feed repo via Comcast DevHub (bare `gh repo create` is blocked — needs
   a `DevHub-Application-ID`), push the local feed, mount it at
   `coverage/requirements/` (gitignored), add `RC-EXT-*` ids + the `RC-*`
   crosswalk, and grow the matrix. The public suite cites only `RC-*` ids; partner
   provenance stays in the private feed, never in this repo.
3. **Real assets.** `assets/manifest.yaml` has placeholder `REPLACE_ME` URLs +
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
