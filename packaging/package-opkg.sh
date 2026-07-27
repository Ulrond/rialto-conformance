#!/usr/bin/env bash
#
# Copyright 2026 RDK Management
# SPDX-License-Identifier: Apache-2.0
#
# Build a single installable opkg .ipk for the conformance suite: the built
# binary + the ut-control runtime lib + a relocatable run wrapper. The VTS build
# system emits this as its artifact; the target team copies one file and
# `opkg install`s it — no build tools or source on the box.
#
# The payload is RELOCATABLE: run.sh derives its own directory, so the same
# package works under any install prefix. Choose the prefix to match the
# python_raft deviceConfig `conformance.installDir` for that target.
#
#   PKG_ARCH=<opkg-arch>  PKG_VERSION=<ver>  PREFIX=</opt/dir>  packaging/package-opkg.sh
#
# Output (last line printed): build/dist/rialto-conformance_<ver>_<arch>.ipk
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

VER="${PKG_VERSION:-1.0.0}"
# opkg's acceptance gate only — the real constraint is the binary's ABI. Set to a
# value in the target's `opkg print-architecture`, or install with
# --force-architecture. Use your fleet's COMMON ARM tune (see README-opkg.md),
# not a single SoC's, so one .ipk serves every ABI-compatible box.
ARCH="${PKG_ARCH:-armv7ahf-neon}"
# Destination folder on the target. Must be writable + executable (not a
# read-only rootfs, not a noexec /tmp). Align with deviceConfig conformance.installDir.
PREFIX="${PREFIX:-/opt/rialto-conformance}"

BIN="${ROOT_DIR}/build/bin/rialto_conformance"
[ -x "${BIN}" ] || { echo "[package-opkg] cross-build first: build/bin/rialto_conformance missing" >&2; exit 1; }

WORK="$(mktemp -d)"; DATA="${WORK}/data"; CTRL="${WORK}/control"
mkdir -p "${DATA}${PREFIX}" "${CTRL}"

# Payload: the binary + the ONE lib the platform does not provide (ut-control).
# libRialtoClient + GStreamer + glib are resolved from the target at runtime.
#
# NO profiles are bundled. The deviceConfig/capability profile is part of the
# HOST test environment, not the target payload: python_raft authors it and
# ships it to ${PREFIX}/deviceConfig.yml at run time (or, for a standalone run,
# drop your box's profile there by hand). The package is entirely box-independent.
cp -a "${BIN}" "${DATA}${PREFIX}/"
cp -a "${ROOT_DIR}/build/bin/libut_control.so" "${DATA}${PREFIX}/"

cat > "${DATA}${PREFIX}/run.sh" <<'EOF'
#!/bin/sh
# Relocatable launcher: sets the lib path relative to itself, drives the LIVE
# RialtoServer over its IPC socket. Any args override the defaults below.
#
# The capability profile (-p) is this platform's HFP (Hardware Feature Profile),
# and it is NOT part of the package. python_raft fetches it host-side and ships
# the resolved file to ${HERE}/hfp.yml; for a standalone run, drop this platform's
# HFP there by hand (or pass one explicitly: run.sh -a -p /path/to/hfp.yml ...).
HERE="$(cd "$(dirname "$0")" && pwd)"
export LD_LIBRARY_PATH="${HERE}:/usr/lib:${LD_LIBRARY_PATH:-}"
export RIALTO_SINKS_RANK="${RIALTO_SINKS_RANK:-1}"
export RIALTO_SOCKET_PATH="${RIALTO_SOCKET_PATH:-/tmp/rialto-0}"
if [ "$#" -eq 0 ]; then
    HFP="${HERE}/hfp.yml"
    if [ ! -f "${HFP}" ]; then
        echo "run.sh: ${HFP} not found — python_raft ships the platform HFP from" >&2
        echo "        the host; for a standalone run, drop this platform's HFP there first." >&2
        exit 1
    fi
    set -- -a -p "${HFP}" -l /tmp/
fi
exec "${HERE}/rialto_conformance" "$@"
EOF
chmod +x "${DATA}${PREFIX}/run.sh" "${DATA}${PREFIX}/rialto_conformance"

SIZE="$(du -ks "${DATA}" | cut -f1)"
cat > "${CTRL}/control" <<EOF
Package: rialto-conformance
Version: ${VER}
Architecture: ${ARCH}
Maintainer: RDK Management
Section: utils
Priority: optional
Installed-Size: ${SIZE}
Description: Rialto interface-conformance suite (ut-core). Installs to ${PREFIX};
 driven on-target by python_raft, or run ${PREFIX}/run.sh directly.
EOF
printf '#!/bin/sh\nchmod +x %s/rialto_conformance %s/run.sh 2>/dev/null || true\nexit 0\n' \
    "${PREFIX}" "${PREFIX}" > "${CTRL}/postinst"
chmod +x "${CTRL}/postinst"

# .ipk = ar(debian-binary, control.tar.gz, data.tar.gz) — member order matters.
cd "${WORK}"; echo "2.0" > debian-binary
tar --numeric-owner --owner=0 --group=0 -czf control.tar.gz -C control .
tar --numeric-owner --owner=0 --group=0 -czf data.tar.gz    -C data .
OUT="${ROOT_DIR}/build/dist/rialto-conformance_${VER}_${ARCH}.ipk"
mkdir -p "${ROOT_DIR}/build/dist"
ar -r "${OUT}" debian-binary control.tar.gz data.tar.gz 2>/dev/null
rm -rf "${WORK}"
echo "[package-opkg] artifact:" >&2
echo "${OUT}"
