<!--
Copyright 2026 RDK Management

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

SPDX-License-Identifier: Apache-2.0
-->

# rialto-conformance

A **version-independent, platform-independent conformance suite** for Rialto's
**northbound external interfaces**, verifying they conform to a catalogue of
**global media-playback conformance requirements**. Each requirement is a
suite-owned, source-neutral `RC-*` id describing what the interface must do; the
provenance of those ids is kept out of this repo (see
[coverage/requirements/](coverage/requirements/)).

It tests two surfaces, and only these — never internal wiring:

- **Surface A — MSE GStreamer sink** — the `rialtomse{audio,video}sink` and
  text-track sink elements (`rialto-gstreamer`): element names, properties, and
  caps negotiation.
- **Surface B — Native client API** — the published C++ interfaces in
  `media/public/include/*` and their client/event callbacks.

The suite is a standalone **installable package**. It links **only** Rialto's
public client library + headers and drives the sink elements present on the
target. By default **it does not build Rialto** — point it at any installed build
and the same cases give the same verdict, so any diff is a real regression or an
intended new behaviour.

**Linux is just another platform.** Where a real target has a hardware-backed
Rialto installed, a Linux host can run the *full* suite against a **software**
Rialto: [build-rialto.sh](build-rialto.sh) builds Rialto + rialto-gstreamer via
their own `NATIVE_BUILD` (platform deps stubbed) into a local prefix that
[build.sh](build.sh) auto-discovers. Same one binary, same cases — only the
backend underneath is software rather than a SoC.

The reproducible, root-clean way to do that is the SC docker flow — one command
that bootstraps anything missing (the `sc` tool, the build-env image) and runs
the build + gate inside the container as you:

```bash
./sc-run.sh                          # CORE gate on the Linux software platform
RIALTO_CONFORMANCE_TIER=all ./sc-run.sh
```

[Dockerfile](Dockerfile) is the build *environment* (toolchain + Rialto's native
deps + the SC user-mapping entrypoint, own ubuntu base — no internal registry or
certificate); [sc-run.sh](sc-run.sh) ensures `sc`
([github.com/rdkcentral/sc](https://github.com/rdkcentral/sc)) + the image exist,
then `sc docker run`s the build + gate against the mounted repo. The sink-surface
(Surface A) cases run green this way; the native client-API (Surface B) cases
additionally need a running **RialtoServer** (the client API is IPC-based) — that
runtime is the next integration step.

This is **real end-to-end testing against real content — not a mock test**. The
verdict is conformance to the published requirements, not an internal contract.

## Harness

| Layer | Role |
|---|---|
| **ut-core** (`VARIANT=CPP` → GoogleTest) | case/assertion structure, run modes, xUnit reporting. Built via its **Makefile** (not CMake). |
| **ut-control** | KVP profile engine, logging, control-plane helpers. Pulled by ut-core's `build.sh`. |
| **ut-raft** (on python_raft) | host-side orchestration: deploy the binary, run it `-a -p <profile>`, adjudicate xUnit. |
| **rialto + rialto-gstreamer** | the API reference the cases are written against (public client headers + the `rialtomse*sink` surface). **Not built here.** |

All of the above are **installed at fixed versions by [install.sh](install.sh)**
into the gitignored `framework/` area — never committed. The pins live in
[framework.lock](framework.lock): ut-core **5.1.0**, python_raft **1.8.2**,
ut-raft **2.1.2**, rialto **v0.22.2**, rialto-gstreamer **v0.20.1**; ut-control
**2.1.0** + GoogleTest **1.15.2** are pulled by ut-core's `build.sh`.

## Install + build

```bash
./install.sh               # clone+pin the framework deps into framework/ (once)
./build.sh                 # build VARIANT=CPP for linux  (runs install.sh if needed)
./build.sh TARGET=arm      # cross-compile for an arm target

# Linux software platform (no hardware Rialto) — build the backend once, then
# build.sh auto-discovers it and the suite runs the full set locally:
./build-rialto.sh          # installs Rialto's native build deps (root) then builds the software stack
./build.sh                 # --no-deps skips the dep install for hosts that already provision them
```

The downstream [Makefile](Makefile) sets `SRC_DIRS`/`INC_DIRS`, takes the public
API headers from the pinned `framework/rialto`, links only `libRialtoClient`
(via pkg-config) + GStreamer, and delegates to ut-core. Output binary:
`build/bin/rialto_conformance`. The suite links the **installed** public client
lib on the host/target; the build itself does not compile Rialto. The exception
is the opt-in Linux software platform — `build-rialto.sh` produces a local
`libRialtoClient` the Makefile then resolves via pkg-config, no source change.

## Run (standalone, on a target with Rialto installed)

```bash
./rialto_conformance -a -p deviceConfig.yaml     # automated xUnit + capability profile
./rialto_conformance -b -p deviceConfig.yaml     # basic stdout
```

In practice ut-raft installs the package and runs it — see [raft/](raft/).

## Test levels (scope of test, not platform)

| Level | Group ID | Scope |
|---|---|---|
| **L1** | `UT_TESTS_L1` | function — each public function on its own: return status, params, state machine; sink element/property behaviour + caps |
| **L2** | `UT_TESTS_L2` | module — one module as a whole (load/attach/play/pause/seek/EOS, position, caps) |
| **L3** | `UT_TESTS_L3` | group — subsystems together so a fault is localisable (SVP, playback, DRM group) |
| **L4** | `UT_TESTS_L4` | full-stream E2E — real elementary streams + real DRM; the §5 coverage matrix is the pass/fail |

Group IDs are **selective-run filters only** (`-e`/`-d`); ut-core runs every
registered suite by default. The same cases run identically on every platform —
only the cross-compiler differs.

## Test tiers (what is being conformed to)

Orthogonal to level, every case declares one tier:

| Tier | Meaning |
|---|---|
| **CORE** | interface conformance — derived from the Rialto external interface contract itself ([coverage/rc-core-catalog.yaml](coverage/rc-core-catalog.yaml)). The **drop-in / transform-safety gate**: a new Rialto must uphold the same external contract as the old one. Run first; must be green. |
| **EXTENDED** | app/player-requirement conformance layered on top; provenance lives in the private requirements feed. |

The L1–L4 group ids are ut-core's **level** axis. Tier is a second, independent
axis the suite selects at runtime — a case declares `CONFORMANCE_CORE_TEST()` or
`CONFORMANCE_EXTENDED_TEST()` at the top of its body (the same self-skip idiom as
the capability/ABI gates), and the active selection is read from the
`RIALTO_CONFORMANCE_TIER` environment variable:

```bash
RIALTO_CONFORMANCE_TIER=core     ./rialto_conformance -a -p deviceConfig.yaml  # the gate
RIALTO_CONFORMANCE_TIER=extended ./rialto_conformance -a -p deviceConfig.yaml
./rialto_conformance -a -p deviceConfig.yaml                                   # both (default)
```

Because tier gating is an in-test skip, it composes with the ut-core level filter
(e.g. `RIALTO_CONFORMANCE_TIER=core ./rialto_conformance -e UT_TESTS_L1`) without
the two contending for the GoogleTest filter.

A requirement (`RC-*` id) is **surface-neutral**. Where the same backend fact is
exposed on both surfaces it is tested **once per path** — a native case and an
MSE case, same id — plus a **consistency** case asserting the two agree. Tests
are never path-agnostic: each case drives exactly one surface as itself.

## Platform applicability is data, not code

One binary, the same cases everywhere. The suite never branches on platform
identity — applicability is a **capability gate** on **platform features**, never
a code fork. The gate is on what the target's *platform* supports/exposes, **not
SoC capability**: a SoC may be capable of something the platform built on it does
not support, and two platforms on one SoC can expose different feature sets — so
there are no per-SoC profiles.

**Only genuinely-variable features are gated.** The standard required surface —
the MSE audio/video/text sinks, the core native interfaces, and baseline
H.264 + AAC — is mandatory and tested **unconditionally**: its absence is a
conformance **failure**, not a skip.

- **End state** — the platform API reports the requirements it exposes; the suite
  reads them at runtime and self-selects its applicable cases.
- **Interim / fallback** — the per-target `deviceConfig` (python_raft shape,
  `deviceConfig: → cpe1: → platform:`; see
  [profiles/deviceConfig.example.yaml](profiles/deviceConfig.example.yaml))
  carries the per-platform feature toggles under `rialto:`. Cases read them with
  `UT_KVP_PROFILE_GET_BOOL("deviceConfig/cpe1/rialto/<key>")` and self-skip via
  `UT_IGNORE_TEST()` when a feature is off. Retired per backend as each gains
  dynamic capability reporting.

Adding a target adds one `deviceConfig` (named by config) and a `raft/` entry —
**no new test code**.

## Layout

```text
install.sh            install pinned framework deps (framework.lock) into framework/
build.sh · Makefile   build VARIANT=CPP; link only libRialtoClient + GStreamer
framework.lock        pinned versions of ut-core / ut-raft / rialto API reference
include/conformance/  CapabilityGate.h · AbiVersion.h · ContentLoader.h · Surfaces.h
src/                  main.cpp + L1_function/ L2_module/ L3_group/ L4_e2e/ (native/ + mse/)
coverage/             matrix.yaml + requirements/ (gitignored private-feed mount)
profiles/             deviceConfig.schema.yaml + deviceConfig.example.yaml  (capability gate)
raft/                 rack_config.yml · device_config.yml · suites/  (deploy/run/adjudicate)
assets/               manifest.yaml — real streams fetched at run start, never committed
packaging/            package.sh — bundle binary + profiles + raft scripts
framework/            install.sh target — ut-core/ut-control/ut-raft/rialto (NOT committed)
```

## Certification model

A backend declares its ABI version via `rialtoPlatformBackendAbiVersion()`;
passing the vN suite certifies that backend at vN (currently **v5**). Bumps are
**additive** — a certified backend is not re-certified by a later additive bump.
The versioned `coverage/matrix.yaml` is the traceability record: "Rialto @ ABI vN
+ backend X meets conformance requirements {RC-…}."

## Licence

Apache-2.0. New files carry the `RDK Management` copyright header.
