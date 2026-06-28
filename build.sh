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
# Makefile. It does NOT build Rialto — the suite links only the public client
# library, resolved on the build host/target.
#
# Usage:
#   ./build.sh                 # linux target, VARIANT=CPP
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

# Pass-through make args (e.g. TARGET=arm) and clean verbs.
MAKE_ARGS=("$@")

echo "[build.sh] building (VARIANT=CPP) ${MAKE_ARGS[*]:-}"
make -C "${ROOT_DIR}" VARIANT=CPP "${MAKE_ARGS[@]:-}"

echo "[build.sh] done -> ${ROOT_DIR}/build/bin/rialto_conformance"
