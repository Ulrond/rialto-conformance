<!--
Copyright 2026 RDK Management
SPDX-License-Identifier: Apache-2.0
-->

# Packaging the conformance suite as an installable `.ipk`

For the VTS build/test team: emit the conformance suite as a **single `.ipk`**
that installs on any ABI-compatible box with `opkg install`. No source, no build
tools, and no orchestration on the target — one file, one install command, into a
destination folder of your choosing.

## Host vs target — what runs where

The suite has two moving parts. Keep them separate:

| Part | Lives / runs on | Role |
|---|---|---|
| **python_raft harness** (`raft/suites/test_rialto_conformance.py`) | **the host** (your CI runner / test host) | Orchestrates: installs the package on the target, runs it, pulls results back, adjudicates. |
| **The `.ipk`** (binary + `libut_control.so` + `run.sh`) | **the target** | The thing that gets installed and executed on the box. |

**deviceConfig stays on the host; the capability profile is the HFP.** Keep the
two roles apart:

- **deviceConfig** (python_raft shape) is **host-only**. python_raft reads it for
  the target's connection + `conformance:` variables (see below), including the
  **URL of this platform's HFP** (`conformance.hfp`). It is never shipped to the
  target.
- **The HFP** (Hardware Feature Profile) is the ut-core KVP profile the on-target
  binary loads with `-p`. It is platform-specific and platform-owned. The host
  **fetches it from the `hfp` URL** and ships the resolved file to the target (to
  `<installDir>/hfp.yml`).

So neither deviceConfig nor any profile is baked into the `.ipk` — the package is
box-independent. **No profiles at all are in the package.** The `.ipk` carries
only the binary, its runtime lib, and the launcher; the HFP is delivered
separately by python_raft (or dropped in by hand for a standalone run).

## Install destination = a python_raft `deviceConfig` variable

The destination folder, result paths, and the HFP URL are **not hard-coded** —
they are `conformance:` variables in the host-side deviceConfig that the raft
harness reads:

```yaml
deviceConfig:
  cpe1:
    conformance:
      installDir: "/opt/rialto-conformance"     # where the package is installed
      binary:     "rialto_conformance"
      resultsDir: "/opt/rialto-conformance/results"
      hfp: "https://<platform-owner>/hfp/<platform>.yaml"   # this platform's HFP
```

Build the `.ipk` with `PREFIX` equal to that `installDir` so the two agree.

Choose a destination that is **writable and executable** on the target:

- `/opt/...` — the usual choice on a locked-down STB (writable, exec allowed).
- **Not** `/tmp` — commonly mounted `noexec` (the binary won't run from there).
- **Not** `/usr` — commonly a read-only rootfs.

The payload is **relocatable** (`run.sh` derives its own directory), so the same
package works under any prefix — the folder is purely a build/config choice.

## What the package contains

```
<installDir>/
  rialto_conformance      # the cross-compiled suite binary (ut-core / GoogleTest)
  libut_control.so        # the only lib the platform does not already provide
  run.sh                  # relocatable launcher (sets LD_LIBRARY_PATH, execs the binary)
```

Everything else the binary needs — `libRialtoClient`, GStreamer 1.x, glib,
libstdc++/libc — is resolved from the **target** at runtime. No Rialto internals
are bundled; the binary links only the public client surface.

## Building the `.ipk`

The `.ipk` wraps an **already cross-compiled** binary (`build/bin/`). Your build
system produces that binary for the target ABI; this step packages it.

### Option A — the script

```sh
PKG_ARCH=<opkg-arch>  PKG_VERSION=<ver>  PREFIX=/opt/rialto-conformance \
    packaging/package-opkg.sh
# -> build/dist/rialto-conformance_<ver>_<arch>.ipk
```

### Option B — manual (to reimplement inside your build system)

An `.ipk` is an `ar` archive of three members, in this order:

```
debian-binary          # the text "2.0"
control.tar.gz         # ./control (+ ./postinst)
data.tar.gz            # ./<installDir>/... (the payload, full target paths)
```

```sh
echo 2.0 > debian-binary
tar --numeric-owner --owner=0 --group=0 -czf control.tar.gz -C control .
tar --numeric-owner --owner=0 --group=0 -czf data.tar.gz    -C data .
ar -r rialto-conformance_<ver>_<arch>.ipk debian-binary control.tar.gz data.tar.gz
```

`control` file:

```
Package: rialto-conformance
Version: <ver>
Architecture: <arch>
Maintainer: RDK Management
Section: utils
Priority: optional
Description: Rialto interface-conformance suite (ut-core).
```

## ARM tuning — build for the common baseline, not one SoC

The `.ipk` filename carries an OE-style arch tuple (e.g. `armv7vet2hf-neon`), but
that string is only opkg's acceptance gate. **The real constraint is the binary's
ABI**, set by the compiler switches. For one package that serves every SoC vendor
in the fleet, build the binary for the **common denominator**, not a single SoC:

- **Float ABI — must be uniform across the fleet.** Use hard-float (`armhf`) if
  every box is hard-float (check for `/lib/ld-linux-armhf.so.3`). A soft/hard
  mismatch will not load. This is the one non-negotiable.
- **CPU profile — use `-march=armv7-a`**, the ARMv7-A common baseline (runs on
  A7/A9/A15/A17 and A53-in-32-bit). Avoid `-march=armv7ve` as the shared baseline
  — it requires virtualization-extension cores and buys nothing here.
- **SIMD — `-mfpu=neon` is a safe common assumption** for STB SoCs (virtually all
  have NEON). If any target lacks it, drop to `-mfpu=vfpv3-d16`.
- `-mthumb` is fine everywhere (Thumb-2 is universal on ARMv7).

So a portable baseline is:

```
-march=armv7-a -mthumb -mfpu=neon -mfloat-abi=hard
```

The suite uses **no SoC-specific intrinsics** — the arch/fpu flags only affect
code generation, never correctness — so the conservative baseline costs
essentially nothing. Override the SDK's default (`armv7ve+neon`) by appending
these to `CFLAGS`/`CXXFLAGS`; the last `-march`/`-mfpu` on the command line wins.

Set the package `Architecture` (`PKG_ARCH`) to a value in the fleet's
`opkg print-architecture`, or install with `--force-architecture` (the ABI is the
real gate). For a fleet spanning ARMv7 **and** AArch64, emit one `.ipk` per ABI
from the same packaging step (`-march=armv8-a`, an aarch64 tuple) — there is no
single arch-independent *binary*; "architecture-independent" here means the
packaging convention and the relocatable layout.

## Installing on the target

Copy one file and install it — this is the whole target-side "install" step:

```sh
scp rialto-conformance_<ver>_<arch>.ipk root@<box>:/tmp/
opkg install --force-architecture /tmp/rialto-conformance_<ver>_<arch>.ipk
```

opkg extracts the payload into `<installDir>` (a plain file copy — `noexec /tmp`
does not block installation; only *running* needs the exec-capable destination).
Uninstall with `opkg remove rialto-conformance`.

## Running (driven by python_raft)

Running is orchestrated from the host by the raft harness, which fetches this
platform's HFP from its `conformance.hfp` URL, ships the resolved file to
`<installDir>/hfp.yml`, and invokes the binary there. For a standalone check
without the harness, drop this platform's HFP at `<installDir>/hfp.yml` and run
the launcher:

```sh
<installDir>/run.sh          # -a -p <installDir>/hfp.yml, xUnit + log to /tmp
```

`run.sh` sets `LD_LIBRARY_PATH` and points `RIALTO_SOCKET_PATH` at the live
RialtoServer (`/tmp/rialto-0` by default). Pass args to narrow scope
(`run.sh -a -e 1 ...` runs L1 only).
