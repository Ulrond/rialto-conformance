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
# Usage:
#   ./build-rialto.sh                 # build the Linux software stack into the prefix
#   ./build-rialto.sh --deps          # first apt-install Rialto's native build deps (needs root)
#   RIALTO_PREFIX=/path ./build-rialto.sh
#
# After it completes, build.sh/Makefile auto-discover the prefix (no env needed):
#   ./build.sh && RIALTO_CONFORMANCE_TIER=core ./build/bin/rialto_conformance -a -p <deviceConfig>
#
# Native build deps (Rialto's own list — install once on the host, needs root):
#   build-essential cmake libunwind-dev libgstreamer1.0-dev
#   libgstreamer-plugins-base1.0-dev libgstreamer-plugins-bad1.0-dev
#   libyaml-cpp-dev protobuf-compiler

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FRAMEWORK_DIR="${ROOT_DIR}/framework"
# Where the built client lib + RialtoClient.pc + headers are installed. The
# Makefile looks here automatically (kept under the gitignored framework/ area).
PREFIX="${RIALTO_PREFIX:-${FRAMEWORK_DIR}/.native-install}"

WITH_DEPS=0
for arg in "$@"; do
    case "${arg}" in
        --deps) WITH_DEPS=1 ;;
        *) echo "[build-rialto] unknown arg: ${arg}" >&2; exit 2 ;;
    esac
done

# Ensure the Rialto sources are cloned at their pinned refs.
if [ ! -d "${FRAMEWORK_DIR}/rialto/.git" ] || [ ! -d "${FRAMEWORK_DIR}/rialto-gstreamer/.git" ]; then
    echo "[build-rialto] Rialto sources missing — running install.sh"
    "${ROOT_DIR}/install.sh"
fi

# Optionally install Rialto's native build dependencies (apt; needs root). We call
# Rialto's own dependency scripts so the list stays in lockstep with the pinned ref.
if [ "${WITH_DEPS}" -eq 1 ]; then
    echo "[build-rialto] installing native build dependencies (root required)"
    SUDO=""; [ "$(id -u)" -ne 0 ] && SUDO="sudo"
    ${SUDO} bash "${FRAMEWORK_DIR}/rialto/install_dependencies_for_native_build.sh"
    if [ -f "${FRAMEWORK_DIR}/rialto-gstreamer/install_dependencies_for_native_build.sh" ]; then
        ${SUDO} bash "${FRAMEWORK_DIR}/rialto-gstreamer/install_dependencies_for_native_build.sh"
    fi
fi

JOBS="$(nproc 2>/dev/null || echo 4)"

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
