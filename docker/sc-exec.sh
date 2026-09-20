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
# sc-exec.sh — run one command inside the SC docker, bootstrapping what is
# missing. The single place the container is entered from, shared by sc-run.sh
# (dev loop), sc-build.sh (build) and emulator.sh (target bring-up).
#
#   1. docker engine present?  (install if absent + privileges allow, else guide)
#   2. sc tool present?        (pip-install from github.com/rdkcentral/sc if absent)
#   3. env image built?        (docker build it if absent)
#   4. run via `sc docker run` (maps you as the in-container user; mounts your
#                               home, so the repo is visible at the same path)
#
# Idempotent: a second call finds sc + the image present and goes straight to the
# run. The command runs with the repo as cwd.
#
# Usage:
#   docker/sc-exec.sh "<command to run inside the container>"
#
# `sc docker run` swallows the inner exit code, so callers must judge the run by
# its log, never by this script's exit status.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${RIALTO_CONFORMANCE_IMAGE:-rialto-conformance-env}"

RUN_CMD="${1:?usage: sc-exec.sh \"<command to run inside the container>\"}"

# 1. docker -----------------------------------------------------------------
if ! command -v docker >/dev/null 2>&1; then
    echo "[sc-exec] docker not found."
    if [ "$(id -u)" -eq 0 ] || sudo -n true 2>/dev/null; then
        SUDO=""; [ "$(id -u)" -ne 0 ] && SUDO="sudo"
        echo "[sc-exec] installing docker.io"
        ${SUDO} apt-get update && ${SUDO} apt-get install -y docker.io
    else
        echo "[sc-exec] ERROR: docker is required. Install the docker engine" >&2
        echo "[sc-exec]         (https://docs.docker.com/engine/install/) and re-run." >&2
        exit 1
    fi
else
    echo "[sc-exec] docker present: $(docker --version)"
fi

# 2. sc ---------------------------------------------------------------------
if ! command -v sc >/dev/null 2>&1; then
    echo "[sc-exec] sc not found — installing from github.com/rdkcentral/sc"
    if ! command -v python3 >/dev/null 2>&1; then
        echo "[sc-exec] ERROR: python3 (3.10+) is required to install sc." >&2; exit 1
    fi
    python3 -m pip install --user --quiet "git+https://github.com/rdkcentral/sc.git@main"
    export PATH="${HOME}/.local/bin:${PATH}"
    command -v sc >/dev/null 2>&1 || { echo "[sc-exec] ERROR: sc install did not land on PATH (${HOME}/.local/bin)." >&2; exit 1; }
else
    echo "[sc-exec] sc present: $(command -v sc)"
fi

# 3. env image --------------------------------------------------------------
# Rebuilt when absent, and when the Dockerfile has moved on since — an image
# older than its recipe is how a tool that was just added to it goes missing.
IMAGE_CREATED="$(docker image inspect -f '{{.Created}}' "${IMAGE}" 2>/dev/null || true)"
if [ -z "${IMAGE_CREATED}" ]; then
    echo "[sc-exec] building env image '${IMAGE}' (first run only)"
    docker build -t "${IMAGE}" "${ROOT_DIR}"
elif [ "$(date -d "${IMAGE_CREATED}" +%s 2>/dev/null || echo 0)" -lt "$(stat -c %Y "${ROOT_DIR}/Dockerfile")" ]; then
    echo "[sc-exec] Dockerfile is newer than image '${IMAGE}' — rebuilding"
    docker build -t "${IMAGE}" "${ROOT_DIR}"
else
    echo "[sc-exec] env image '${IMAGE}' already present"
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
    echo "[sc-exec] dropping active venv '${VIRTUAL_ENV}' so sc can use its own pyenv 'sc' env"
    SC_PATH="$(printf '%s' "${PATH}" | tr ':' '\n' | grep -vF "${VIRTUAL_ENV}/bin" | paste -sd: -)"
fi

echo "[sc-exec] sc docker run -l ${IMAGE} -- <command>"
exec env -u VIRTUAL_ENV -u PYENV_VERSION PATH="${SC_PATH}" sc docker run -l "${IMAGE}" -- "${RUN_CMD}"
