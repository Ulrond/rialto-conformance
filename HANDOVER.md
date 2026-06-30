<!--
Copyright 2026 RDK Management
SPDX-License-Identifier: Apache-2.0
-->

# HANDOVER — rialto-conformance

**Self-contained session-starter.** Read this to pick the project back up: current
state, the invariants that must not regress, and exactly where to start next.

## Read first (in order)

1. This file (state + invariants + next actions).
2. [README.md](README.md) — what the suite is and how it is shaped.
3. [coverage/rc-core-catalog.yaml](coverage/rc-core-catalog.yaml) — the 118
   `RC-CORE-*` interface-conformance requirements (the Phase 1 spec).
4. [sc-run.sh](sc-run.sh) + [docker/run-in-container.sh](docker/run-in-container.sh)
   — the one-shot Linux software-platform build+run.

## Where things are

| What | Location |
|---|---|
| **This repo (the suite)** | `/home/gew04/git/fast/sky/rialto-conformance` — **published**, public: [Ulrond/rialto-conformance](https://github.com/Ulrond/rialto-conformance) |
| **Private requirements feed** | `/home/gew04/git/fast/sky/rialto-conformance-requirements` — local git; comcast-sky push **blocked** (org needs a `DevHub-Application-ID` at repo creation). Mounts at `coverage/requirements/` (gitignored). Phase 2. |
| **Rialto API reference** | cloned by `install.sh` into `framework/rialto` + `framework/rialto-gstreamer` at the `framework.lock` pins (rialto **v0.22.2**) |

**git-flow** (via the `sc` tool's model, plain git): `master` = published baseline,
**`develop`** = integration **(GitHub default branch)**, issue-numbered
`feature/*` branches → PR → merge into `develop`. The bring-up (venv,
build-rialto, SC docker, RialtoServer runtime), the IControl + factory + capability
+ IClientLogControl L1 cases, and the `sc` venv-shadow fix are all merged.
**Open issues:** #14 (CONTROL-002 callback, blocked by #18), #17 (clearkey CDM),
#18 (software render), and #22 (real assets). PRs go green-as-designed via
`./sc-run.sh` and are merged once proven.

**gh accounts / tokens.** Two accounts: **Ulrond** (public repo; orgs
sky-uk/rdkcentral/qtasks) and **gerald-weatherup_comcast** (only one that reaches
comcast-sky). Use per-command tokens (`gh auth token --user <acct>` → `GH_TOKEN` /
token-over-HTTPS push), **never `gh auth switch`**.

## Status

Phase 1 (CORE interface conformance) is well underway and **runs end-to-end on a
Linux software platform via the SC docker flow**:

- **Source-neutral & published.** No partner/app names anywhere in the public
  tree (enforced); requirements cited only by suite-owned `RC-*` ids. History was
  squashed clean before first push — do not restore pre-scrub history.
- **CORE catalogue: 121 `RC-CORE-*` requirements**, header-verified to cover the
  whole external surface. **~24 authored as cases; ~97 catalogued, not yet
  written** (the authored set: CONTROL, FACTORY, CAPS, KEYSCAP-API, LOG, plus the
  MSE element-registration smoke).
- **The gate runs.** The software Rialto and the suite are built, a `RialtoServer`
  is brought up (Active), and the CORE gate runs against it:
  **23 tests → 18 PASS, 3 SKIP (capability-gated), 2 FAIL.** Both fails are
  `NATIVE_BUILD` **stub-OCDM** properties (`RC-CORE-KEYSCAP-002/003`): the stub
  accepts any key system and reports no version, so it is correctly flagged as a
  non-conformant DRM backend — **not** a suite bug; a real CDM is expected to
  pass them. Do not weaken those cases to force green.

## Run

```bash
# Linux software platform (no hardware Rialto), reproducible + root-clean:
./sc-run.sh                              # ensure sc + image (only if missing), then
                                         # sc docker run: build sw Rialto + suite,
                                         # start RialtoServer, run the CORE gate
RIALTO_CONFORMANCE_TIER=all ./sc-run.sh  # both tiers

# Plain (non-SC) build on a host/target that already has Rialto installed:
./install.sh && ./build.sh
RIALTO_CONFORMANCE_TIER=core ./build/bin/rialto_conformance -a -p profiles/deviceConfig.linux.yaml
```

`sc docker run` swallows the inner exit code — **always read the run log**, never
trust the wrapper exit. Same for backgrounded `docker build` (the notification
exit is the wrapper's, not docker's).

## Invariants (DO NOT regress)

1. **Source-neutral public repo.** Cite requirements by `RC-*` ids only; partner /
   app / programme provenance lives solely in the private feed — never any source
   names in the public tree (incl. git history + commit messages). Re-grep before
   pushing; the private feed's README lists what must stay out.
2. **Platform-agnostic; gate on platform FEATURES, not SoC.** One binary, same
   cases everywhere. No per-SoC profiles. `deviceConfig` (python_raft shape,
   `deviceConfig: → cpe1: → rialto:`) carries the variable-feature toggles; cases
   self-skip via `CONFORMANCE_REQUIRE_CAP("feature.key")` ([CapabilityGate.h](include/conformance/CapabilityGate.h)).
3. **Only variable features are gated.** The standard required surface (MSE
   audio/video/text sinks, core native interfaces, baseline H.264 + AAC) is tested
   **unconditionally** — absence is a FAILURE, not a skip.
4. **Two tiers, orthogonal to L1–L4.** CORE (interface, the transform-safety gate)
   vs EXTENDED (app/player, Phase 2). A case declares `CONFORMANCE_CORE_TEST()` /
   `CONFORMANCE_EXTENDED_TEST()` ([TierGate.h](include/conformance/TierGate.h));
   selected at runtime by `RIALTO_CONFORMANCE_TIER` (in-test self-skip, composes
   with the `-e UT_TESTS_Ln` level groups — ut-core owns the L1–L4 group enum).
5. **Surface-neutral requirements, per-surface cases.** A requirement exposed on
   both surfaces gets one case per path (native + mse) + a `path: both` consistency
   case. Tests are never path-agnostic — no wrapper runs one body on both paths.
6. **Release-targeted, NOT ABI-versioned.** The suite targets one Rialto release
   (the `framework.lock` pin; `kTargetRialtoRelease` / `targetRialtoRelease` =
   v0.22.2). A requirement may declare a `since:` release;
   `CONFORMANCE_REQUIRE_SINCE("vX")` self-skips on an older target. This is **not**
   Rialto's binary ABI (fixed per release). ([RialtoRelease.h](include/conformance/RialtoRelease.h)).
7. **Never override ut-core's build flags.** Contribute includes via `INC_DIRS`
   and libs via `YLDFLAGS` (the sanctioned ut-core caller hooks) — ut-core resets
   `XCFLAGS` and compiles `.cpp` with `CXXFLAGS`, so a caller's flags must not
   touch them. See [Makefile](Makefile).
8. **Rialto is linked, not built — except the opt-in Linux software platform.**
   Default: link an installed `libRialtoClient`. `build-rialto.sh` opt-in builds a
   software Rialto (NATIVE_BUILD, platform deps stubbed) into
   `framework/.native-install`, which the Makefile auto-discovers.

## What exists now

- `install.sh` (+ `framework.lock`, venv-isolated raft deps), `build.sh` + `Makefile`.
- `build-rialto.sh` (software Rialto; installs Rialto's apt deps by default,
  `--no-deps` to skip), `Dockerfile` (build-env), `docker/{entrypoint,bashext,run-in-container}.sh`,
  `sc-run.sh` (the SC one-shot).
- `include/conformance/`: CapabilityGate, RialtoRelease, TierGate, ContentLoader, Surfaces.
- `src/main.cpp` + `src/common/Surfaces.cpp`; **authored L1 cases**:
  `native/CapabilitiesTests` (incl. CAPS-003/004), `native/KeysCapabilitiesTests`,
  `native/ControlTests`, `native/FactoryTests`, `native/ClientLogControlTests`,
  `mse/SinkRegistrationTests`. L2–L4 trees scaffolded (empty).
- `coverage/rc-core-catalog.yaml` (121 reqs) + `coverage/matrix.yaml` (coverage
  view: tier + path + case + status, `targetRialtoRelease`); `coverage/requirements/`
  mount README; `profiles/` (schema + example + `deviceConfig.linux.yaml`);
  `raft/` (rack + device config + adjudicator); `assets/manifest.yaml`;
  `packaging/package.sh`.

## Start here next

Open issues: **#14** (CONTROL-002 callback-on-transition — **blocked by #18**),
**#17** (clearkey CDM), **#18** (software render for L4), **#22** (real assets).

The climb is grouped by what passes on the software platform **now** vs. what is
stub-gated. Continue the green batches (author → `./sc-run.sh` → merge once proven),
then the gated areas as #17/#18/#22 land:

1. **MSE Surface A introspection** *(green, the big next batch — several PRs)* —
   the `rialtomse*sink` element contracts, which need only the sink elements
   (`RIALTO_SINKS_RANK`), no live server: `RC-CORE-MSEPROP-*` (GObject properties),
   `RC-CORE-MSECAPS-*` (pad templates / MIME→caps), `RC-CORE-MSESTATE-*` (state
   transitions), `RC-CORE-MSEEVENT-*` (events/queries), and the remaining
   `RC-CORE-MSE-*`. One PR per sub-area; extend `src/L1_function/mse/`.
2. **IWebAudioPlayer L1** (`RC-CORE-WEBAUDIO-*`) — verify whether the software
   stub supports web audio; gate/skip what it cannot.
3. **The green `IMediaPipeline` subset** — create/getClient/synchronous getters
   that do not need playback (the playback/data path is gated on #18).
4. **Clearkey CDM (#17)** — replace the stub OCDM with a clearkey software CDM,
   **swappable with a real CDM later**; unblocks `RC-CORE-KEYS-*` and the two
   currently-failing `RC-CORE-KEYSCAP-002/003`.
5. **Software render (#18) + real assets (#22)** — implement the software render
   path and `ContentLoader` against free-to-use clips (downloadable, ideally
   YouTube test streams; `assets/manifest.yaml` has placeholder `REPLACE_ME`
   URLs). Together these unblock the `RC-CORE-PIPE-*`/`DATA-*` playback set, the
   L4 E2E matrix, and the CONTROL-002 callback (#14).
6. **Phase 2 — EXTENDED / app-requirement conformance** *(deferred until the CORE
   items above are working).* Create the comcast-sky private feed repo via
   Comcast DevHub (bare `gh repo create` blocked — needs a `DevHub-Application-ID`),
   push the local feed, mount at `coverage/requirements/`, add `RC-EXT-*` ids +
   the `RC-*` crosswalk.

## Open items / to verify on real hardware

- **Stub-OCDM fails are expected on the software platform**, not on a real CDM —
  the 2 KEYSCAP failures distinguish the stub from a conformant DRM backend.
- **ut-raft API touchpoints** in `raft/suites/test_rialto_conformance.py` (the
  `_copy_to/from_target` file transfer) — verify against installed ut-raft 2.1.2
  on a real rack.
- **`since:` per requirement** — add real "introduced-in release" values when the
  suite spans multiple Rialto releases (today everything targets the pin).
- **Private-feed copyright/classification** — the local feed uses a placeholder
  `Comcast / Sky CONFIDENTIAL` header; confirm before publishing to comcast-sky.
