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
# Build the conformance binary (VARIANT=CPP → GoogleTest).
#
# Framework deps (ut-core, ut-raft, the Rialto API reference, ...) are INSTALLED
# at fixed versions by install.sh into the gitignored framework/ area, never
# committed. This script ensures they are installed, then delegates to ut-core's
# Makefile. By default it does NOT build Rialto — the suite links an installed
# libRialtoClient, resolved on the build host/target (correct for real targets).
#
# To run on a Linux host with no hardware Rialto, build the software stack first
# with ./build-rialto.sh; the Makefile then auto-discovers it. See build-rialto.sh.
#
# Building and testing are separate (issue #104): this script builds and packages,
# ./test.sh runs. A successful build leaves the deployable tarball in build/dist,
# which is exactly what test.sh ships to the target — nothing in the test path
# ever compiles.
#
# Usage:
#   ./build.sh                 # linux target, VARIANT=CPP, then package
#   ./build.sh --no-package    # compile only, skip the tarball
#   ./build.sh TARGET=arm      # arm cross-compile (toolchain sourced from env)
#   ./build.sh clean | cleanall

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
UT_CORE_DIR="${ROOT_DIR}/framework/ut-core"

# Ensure the framework is installed at its pinned versions (idempotent).
if [ ! -f "${UT_CORE_DIR}/Makefile" ]; then
    echo "[build.sh] framework not installed yet — running install.sh"
    "${ROOT_DIR}/install.sh"
fi

# Pass-through make args (e.g. TARGET=arm) and clean verbs. --no-package is ours.
PACKAGE=1
MAKE_ARGS=()
for arg in "$@"; do
    case "${arg}" in
        --no-package) PACKAGE=0 ;;
        *)            MAKE_ARGS+=("${arg}") ;;
    esac
done

echo "[build.sh] building (VARIANT=CPP) ${MAKE_ARGS[*]-}"
# Safe expansion: pass MAKE_ARGS only when non-empty (an empty "${a[@]:-}" expands
# to a single empty argument, which make rejects as an invalid file name).
make -C "${ROOT_DIR}" VARIANT=CPP ${MAKE_ARGS[@]+"${MAKE_ARGS[@]}"}

echo "[build.sh] done -> ${ROOT_DIR}/build/bin/rialto_conformance"

# Package unless told not to, and never for a clean verb — test.sh consumes this.
case " ${MAKE_ARGS[*]-} " in
    *" clean "*|*" cleanall "*) PACKAGE=0 ;;
esac
if [ "${PACKAGE}" -eq 1 ]; then
    echo "[build.sh] packaging"
    ARTIFACT="$("${ROOT_DIR}/packaging/package.sh")"
    echo "[build.sh] package -> ${ARTIFACT}"
fi
