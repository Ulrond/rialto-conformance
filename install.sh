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
# Install the framework testing suite at fixed versions (§4).
#
# Clones every dependency in framework.lock into the gitignored framework/ area
# at its exact pinned ref — only if not already present (the ut-raft clone_repo
# idiom). These deps are INSTALLED, never committed. ut-core's own build.sh then
# pulls ut-control + GoogleTest at its pinned versions during the build.
#
#   ut-core           — the test runner (VARIANT=CPP / GoogleTest)
#   python_raft + ut-raft — host-side orchestration (deploy/run/adjudicate)
#   rialto + rialto-gstreamer — the API reference the cases are written against
#                       (public client headers + the rialtomse*sink surface).
#                       The suite does NOT build Rialto; these are the reference
#                       for compiling/checking against the public API. The
#                       runtime lib (libRialtoClient) comes from the installed
#                       Rialto on the build host/target via pkg-config.
#
# Run:  ./install.sh        (then ./build.sh to build)

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FRAMEWORK_DIR="${ROOT_DIR}/framework"
LOCK_FILE="${ROOT_DIR}/framework.lock"

mkdir -p "${FRAMEWORK_DIR}"

# Clone a repo at a fixed ref, only if the target path is absent.
clone_repo()
{
    local name="$1" repo_url="$2" ref="$3"
    local path="${FRAMEWORK_DIR}/${name}"

    if [ -d "${path}/.git" ]; then
        echo "[install.sh] ${name}: present, checking out pinned ${ref}"
        git -C "${path}" fetch --quiet --tags origin || true
        git -C "${path}" checkout --quiet "${ref}"
        return
    fi

    echo "[install.sh] cloning ${name} @ ${ref}"
    git clone --quiet "${repo_url}" "${path}"
    git -C "${path}" checkout --quiet "${ref}"
}

echo "[install.sh] installing framework deps from framework.lock (fixed versions, never committed)"
while read -r name repo_url ref _role; do
    case "${name}" in
        ''|\#*) continue ;;  # skip blanks + comments
    esac
    clone_repo "${name}" "${repo_url}" "${ref}"
done < "${LOCK_FILE}"

# Install the python_raft / ut-raft host requirements if pip is available.
if command -v pip >/dev/null 2>&1; then
    for req in "${FRAMEWORK_DIR}/python_raft/requirements.txt" "${FRAMEWORK_DIR}/ut-raft/requirements.txt"; do
        if [ -f "${req}" ]; then
            echo "[install.sh] pip install -r ${req#${ROOT_DIR}/}"
            pip install -q -r "${req}" || echo "[install.sh] WARN: pip install failed for ${req}"
        fi
    done
fi

echo "[install.sh] framework installed under framework/ (gitignored). Next: ./build.sh"
