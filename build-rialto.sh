#!/usr/bin/env bash

#/**
# * Copyright 2026 RDK Management
# *
# * Licensed under the Apache License, Version 2.0 (the "License");
# * you may not use this file except in compliance with the License.
# * You may obtain a copy of the License at
# *
# * http://www.apache.org/licenses/LICENSE-2.0
# *
# * Unless required by applicable law or agreed to in writing, software
# * distributed under the License is distributed on an "AS IS" BASIS,
# * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# * See the License for the specific language governing permissions and
# * limitations under the License.
# *
# * SPDX-License-Identifier: Apache-2.0
# */
#
# Build the Rialto software stack so the suite can link a real libRialtoClient and
# run the FULL suite on a Linux host with no hardware Rialto installed.
#
# Linux is treated as just another platform: underneath is a SOFTWARE backend
# rather than a SoC. Rialto's NATIVE_BUILD stubs the platform deps (opencdm,
# wpeframework, rdk_gstreamer_utils), so `framework/rialto` + `framework/rialto-
# gstreamer` build into a self-contained software stack — libRialtoClient (Surface
# B) and the rialtomse*sink plugin (Surface A).
#
# This is OPT-IN. The default suite flow (install.sh + build.sh) links an
# *installed* libRialtoClient and never builds Rialto — that is correct for real
# targets, where the platform Rialto already exists. Run this only when you want a
# local software platform to run against.
#
# The native build deps are a REQUIREMENT, not an option — so this installs them
# by default (Rialto's own apt list; needs root / passwordless sudo) whenever they
# are missing, then builds. Use --no-deps only on a host that already provisions
# them (CI image, etc.), where it errors out instead if any are absent.
#
# Usage:
#   ./build-rialto.sh                 # ensure deps (install if missing) then build
#   ./build-rialto.sh --no-deps       # assume deps present; do not apt-install (error if missing)
#   RIALTO_PREFIX=/path ./build-rialto.sh
#
# After it completes, build.sh/Makefile auto-discover the prefix (no env needed):
#   ./build.sh && RIALTO_CONFORMANCE_TIER=core ./build/bin/rialto_conformance -a -p <deviceConfig>
#
# Native build deps (Rialto's own list, installed by default):
#   build-essential cmake libunwind-dev libgstreamer1.0-dev
#   libgstreamer-plugins-base1.0-dev libgstreamer-plugins-bad1.0-dev
#   libyaml-cpp-dev protobuf-compiler
#
# TESTED STATUS: dependency detection + both control paths (default auto-install,
# and --no-deps error) are verified, as is the cmake invocation up to the Protobuf
# gate (cleared with protoc/libprotobuf staged). The FULL build is NOT yet verified
# end-to-end on this dev box — it has no root, and the deps install (and thus the
# native build) requires root. Run on a privileged Linux host to validate fully.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FRAMEWORK_DIR="${ROOT_DIR}/framework"
# Where the built client lib + RialtoClient.pc + headers are installed. The
# Makefile looks here automatically (kept under the gitignored framework/ area).
PREFIX="${RIALTO_PREFIX:-${FRAMEWORK_DIR}/.native-install}"

NO_DEPS=0
for arg in "$@"; do
    case "${arg}" in
        --no-deps) NO_DEPS=1 ;;
        *) echo "[build-rialto] unknown arg: ${arg}" >&2; exit 2 ;;
    esac
done

# Echo the apt package names of any absent native build prerequisites, one per
# line (empty output => all present). Names match Rialto's own
# install_dependencies_for_native_build.sh.
missing_prereqs()
{
    command -v cmake      >/dev/null 2>&1 || echo "cmake"
    command -v make       >/dev/null 2>&1 || echo "build-essential"
    command -v g++        >/dev/null 2>&1 || echo "g++"
    command -v protoc     >/dev/null 2>&1 || echo "protobuf-compiler"
    command -v pkg-config >/dev/null 2>&1 || echo "pkg-config"
    if command -v pkg-config >/dev/null 2>&1; then
        pkg-config --exists protobuf         2>/dev/null || echo "libprotobuf-dev"
        pkg-config --exists gstreamer-1.0     2>/dev/null || echo "libgstreamer1.0-dev"
        pkg-config --exists gstreamer-app-1.0 2>/dev/null || echo "libgstreamer-plugins-base1.0-dev"
        pkg-config --exists yaml-cpp          2>/dev/null || echo "libyaml-cpp-dev"
    fi
    pkg-config --exists libunwind 2>/dev/null || [ -e /usr/include/libunwind.h ] || echo "libunwind-dev"
}

# Install Rialto's native build deps (a REQUIREMENT, so done by default). Uses
# Rialto's own dependency scripts so the list stays in lockstep with the pinned
# ref. Needs root or passwordless sudo; errors clearly if neither is available.
install_deps()
{
    local sudo=""
    if [ "$(id -u)" -ne 0 ]; then
        if sudo -n true 2>/dev/null; then
            sudo="sudo"
        else
            echo "[build-rialto] ERROR: native build dependencies are required but installing them needs root." >&2
            echo "[build-rialto] Re-run as root or with passwordless sudo, or install the deps yourself and use --no-deps:" >&2
            missing_prereqs | sed 's/^/  - /' >&2
            exit 1
        fi
    fi
    echo "[build-rialto] installing native build dependencies (Rialto's apt list)"
    ${sudo} bash "${FRAMEWORK_DIR}/rialto/install_dependencies_for_native_build.sh"
    if [ -f "${FRAMEWORK_DIR}/rialto-gstreamer/install_dependencies_for_native_build.sh" ]; then
        ${sudo} bash "${FRAMEWORK_DIR}/rialto-gstreamer/install_dependencies_for_native_build.sh"
    fi
}

# Ensure the Rialto sources are cloned at their pinned refs.
if [ ! -d "${FRAMEWORK_DIR}/rialto/.git" ] || [ ! -d "${FRAMEWORK_DIR}/rialto-gstreamer/.git" ]; then
    echo "[build-rialto] Rialto sources missing — running install.sh"
    "${ROOT_DIR}/install.sh"
fi

# Ensure the native build dependencies are present. They are a REQUIREMENT, so by
# default we install them when missing; --no-deps turns the absence into an error
# instead (for hosts that provision deps externally).
if [ -n "$(missing_prereqs)" ]; then
    if [ "${NO_DEPS}" -eq 1 ]; then
        echo "[build-rialto] ERROR: required native build dependencies are missing (--no-deps set):" >&2
        missing_prereqs | sed 's/^/  - /' >&2
        echo "[build-rialto] install them, or drop --no-deps to install automatically." >&2
        exit 1
    fi
    install_deps
    if [ -n "$(missing_prereqs)" ]; then
        echo "[build-rialto] ERROR: dependencies still missing after install:" >&2
        missing_prereqs | sed 's/^/  - /' >&2
        exit 1
    fi
fi
echo "[build-rialto] native build prerequisites present"

JOBS="$(nproc 2>/dev/null || echo 4)"

# 0. Select the OpenCDM backend for Rialto's NATIVE_BUILD `libocdm`. Rialto builds
#    libocdm from stubs/opencdm/*.cpp; RIALTO_OCDM_BACKEND picks which open_cdm.cpp
#    body it compiles (the ABI is unchanged, so a real CDM can be swapped in later):
#      clearkey (default) - the suite's W3C ClearKey CDM (backends/opencdm/clearkey):
#                           org.w3.clearkey supported + versioned, others rejected,
#                           no server certificate. Makes IMediaKeysCapabilities
#                           conformant in software CI.
#      stub               - Rialto's pristine permissive stub (accepts any key
#                           system, reports no version).
#    The stub source is reset from the pinned checkout first, so the choice is
#    deterministic regardless of a previous overlay.
OCDM_BACKEND="${RIALTO_OCDM_BACKEND:-clearkey}"
OCDM_STUB="${FRAMEWORK_DIR}/rialto/stubs/opencdm/open_cdm.cpp"
echo "[build-rialto] selecting OpenCDM backend: ${OCDM_BACKEND}"
git -C "${FRAMEWORK_DIR}/rialto" checkout --quiet -- stubs/opencdm/open_cdm.cpp 2>/dev/null || true
case "${OCDM_BACKEND}" in
    stub)
        : # leave Rialto's pristine stub in place
        ;;
    clearkey)
        cp "${ROOT_DIR}/backends/opencdm/clearkey/open_cdm.cpp" "${OCDM_STUB}"
        ;;
    *)
        echo "[build-rialto] ERROR: unknown RIALTO_OCDM_BACKEND '${OCDM_BACKEND}' (want: clearkey|stub)" >&2
        exit 2
        ;;
esac

# 1. Build Rialto (client + server) natively and install into PREFIX so that
#    RialtoClient.pc resolves to the built library.
echo "[build-rialto] building Rialto (NATIVE_BUILD) -> ${PREFIX}"
cmake -S "${FRAMEWORK_DIR}/rialto" -B "${FRAMEWORK_DIR}/rialto/build" \
      -DNATIVE_BUILD=ON -DRIALTO_BUILD_TYPE="Debug" \
      -DCMAKE_INSTALL_PREFIX="${PREFIX}"
make -C "${FRAMEWORK_DIR}/rialto/build" -j"${JOBS}"
make -C "${FRAMEWORK_DIR}/rialto/build" install

# 2. Build rialto-gstreamer natively against the installed Rialto, providing the
#    rialtomse*sink plugin (Surface A).
echo "[build-rialto] building rialto-gstreamer (NATIVE_BUILD) against ${PREFIX}"
cmake -S "${FRAMEWORK_DIR}/rialto-gstreamer" -B "${FRAMEWORK_DIR}/rialto-gstreamer/build" \
      -DNATIVE_BUILD=ON -DRIALTO_BUILD_TYPE="Debug" \
      -DCMAKE_INCLUDE_PATH="${PREFIX}/include" -DCMAKE_LIBRARY_PATH="${PREFIX}/lib" \
      -DCMAKE_INSTALL_PREFIX="${PREFIX}"
make -C "${FRAMEWORK_DIR}/rialto-gstreamer/build" -j"${JOBS}"
make -C "${FRAMEWORK_DIR}/rialto-gstreamer/build" install || true  # plugin .so may be used in-tree

echo "[build-rialto] done. Rialto software stack installed under ${PREFIX}"
echo "[build-rialto]   libRialtoClient + RialtoClient.pc : ${PREFIX}/lib"
echo "[build-rialto]   the suite's Makefile finds this automatically — next: ./build.sh"
echo "[build-rialto]   at runtime, point GStreamer at the built plugin:"
echo "[build-rialto]     export GST_PLUGIN_PATH=${FRAMEWORK_DIR}/rialto-gstreamer/build:\${GST_PLUGIN_PATH:-}"
