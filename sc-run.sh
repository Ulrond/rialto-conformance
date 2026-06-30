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
# One-shot: run the full Linux software-platform build + CORE gate inside the SC
# docker, bootstrapping anything missing — but ONLY if missing.
#
#   1. docker engine present?  (install if absent + privileges allow, else guide)
#   2. sc tool present?        (pip-install from github.com/rdkcentral/sc if absent)
#   3. env image built?        (docker build it if absent)
#   4. run via `sc docker run` (maps you as the in-container user; mounts the repo)
#
# Idempotent: a second run finds sc + the image already present and goes straight
# to step 4.
#
# Usage:
#   ./sc-run.sh                       # build + run the CORE gate (default)
#   RIALTO_CONFORMANCE_TIER=all ./sc-run.sh
#   ./sc-run.sh -- "<custom command to run inside the container>"

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMAGE="${RIALTO_CONFORMANCE_IMAGE:-rialto-conformance-env}"
TIER="${RIALTO_CONFORMANCE_TIER:-core}"

# Default in-container command: build the software Rialto + suite, bring up a
# RialtoServer, and run the gate — all in docker/run-in-container.sh.
DEFAULT_CMD="RIALTO_CONFORMANCE_TIER=${TIER} ./docker/run-in-container.sh"

RUN_CMD="${DEFAULT_CMD}"
if [ "${1:-}" = "--" ]; then shift; RUN_CMD="$*"; fi

# 1. docker -----------------------------------------------------------------
if ! command -v docker >/dev/null 2>&1; then
    echo "[sc-run] docker not found."
    if [ "$(id -u)" -eq 0 ] || sudo -n true 2>/dev/null; then
        SUDO=""; [ "$(id -u)" -ne 0 ] && SUDO="sudo"
        echo "[sc-run] installing docker.io"
        ${SUDO} apt-get update && ${SUDO} apt-get install -y docker.io
    else
        echo "[sc-run] ERROR: docker is required. Install the docker engine" >&2
        echo "[sc-run]        (https://docs.docker.com/engine/install/) and re-run." >&2
        exit 1
    fi
else
    echo "[sc-run] docker present: $(docker --version)"
fi

# 2. sc ---------------------------------------------------------------------
if ! command -v sc >/dev/null 2>&1; then
    echo "[sc-run] sc not found — installing from github.com/rdkcentral/sc"
    if ! command -v python3 >/dev/null 2>&1; then
        echo "[sc-run] ERROR: python3 (3.10+) is required to install sc." >&2; exit 1
    fi
    python3 -m pip install --user --quiet "git+https://github.com/rdkcentral/sc.git@main"
    export PATH="${HOME}/.local/bin:${PATH}"
    command -v sc >/dev/null 2>&1 || { echo "[sc-run] ERROR: sc install did not land on PATH (${HOME}/.local/bin)." >&2; exit 1; }
else
    echo "[sc-run] sc present: $(command -v sc)"
fi

# 3. env image --------------------------------------------------------------
if ! docker image inspect "${IMAGE}" >/dev/null 2>&1; then
    echo "[sc-run] building env image '${IMAGE}' (first run only)"
    docker build -t "${IMAGE}" "${ROOT_DIR}"
else
    echo "[sc-run] env image '${IMAGE}' already present"
fi

# 4. run via sc -------------------------------------------------------------
# sc activates its own pyenv virtualenv ('sc', which carries the `sc` python
# module) internally and runs `python3 -m sc`. If the suite's python_venv (the
# raft host-deps venv from install.sh) is active in the caller's shell it shadows
# that env: `python3 -m sc` then runs under python_venv, `import sc` fails, and sc
# aborts with "SC not found". Hand off to sc with the suite venv stripped from the
# environment so sc can select its own interpreter.
SC_PATH="${PATH}"
if [ -n "${VIRTUAL_ENV:-}" ]; then
    echo "[sc-run] dropping active venv '${VIRTUAL_ENV}' so sc can use its own pyenv 'sc' env"
    SC_PATH="$(printf '%s' "${PATH}" | tr ':' '\n' | grep -vF "${VIRTUAL_ENV}/bin" | paste -sd: -)"
fi

echo "[sc-run] sc docker run -l ${IMAGE} -- <build + gate>"
exec env -u VIRTUAL_ENV -u PYENV_VERSION PATH="${SC_PATH}" sc docker run -l "${IMAGE}" -- "${RUN_CMD}"
