<!--
Copyright 2026 RDK Management
SPDX-License-Identifier: Apache-2.0
-->

# OpenCDM backends

Suite-owned OpenCDM implementations that back Rialto's `libocdm` on the Linux
software platform. Rialto's NATIVE_BUILD compiles `libocdm` from
`framework/rialto/stubs/opencdm/*.cpp`; `build-rialto.sh` overlays the selected
backend's `open_cdm.cpp` over that stub before configuring, so the built
`libocdm` — and therefore `IMediaKeys` / `IMediaKeysCapabilities` — uses it.

The interface is the stable OpenCDM C ABI, so the backend is selected without
touching the suite or Rialto sources, and a real CDM can be dropped in later.

## Selecting a backend

`build-rialto.sh` reads `RIALTO_OCDM_BACKEND`:

| Value | Backend | `libocdm` behaviour |
|---|---|---|
| `clearkey` (default) | `clearkey/open_cdm.cpp` | W3C ClearKey CDM: `org.w3.clearkey` supported + versioned, all other key systems rejected, no server certificate |
| `stub` | *(none — Rialto's upstream stub)* | permissive no-op: every key system reported supported, `create_system` always fails |

```bash
./build-rialto.sh                          # clearkey (default)
RIALTO_OCDM_BACKEND=stub ./build-rialto.sh # Rialto's pristine permissive stub
```

The overlay is applied over a pristine checkout each build (the stub file is
reset first), so the backend source of truth stays here in the suite and the
selection is deterministic and repeatable.

## `clearkey`

A minimal, honest W3C [Clear Key](https://www.w3.org/TR/encrypted-media/#clear-key)
CDM. It makes the DRM capability surface conformant in software CI:
`supportsKeySystem`/`getSupportedKeySystemVersion`/`isServerCertificateSupported`
answer correctly for `org.w3.clearkey` and reject unknown systems, so
`RC-CORE-KEYSCAP-002/003` pass and the ClearKey capability cases run against a
real backend (gated on the `drm.clearkey` platform feature).

The session lifecycle is a coherent placeholder (a valid, destructible handle
reporting `Usable`); full ClearKey session/decrypt lands with the software data
path.
