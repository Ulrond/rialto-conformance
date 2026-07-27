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
# run-linux.sh — run the conformance suite on a LINUX Rialto box via python_raft.
#
# The friendly entry for the raft flow: python_raft builds the suite if it is
# not already built (packaging/package.sh -> build.sh), connects to the box,
# copies the binary across, then runs the cases from the host — exec'ing the
# binary on the target and adjudicating the xUnit it returns. The box is a
# render-capable Linux VM (its sinks actually render) or a local software Rialto;
# pick it by slot in raft/rack_config.linux.yml (edit the slot `ip`).
#
# Usage:
#   ./run-linux.sh                              # rack1 / linux-native (default)
#   ./run-linux.sh --slotName lab-linux-2       # a different Linux slot
#   ./run-linux.sh --rack rack1 --slotName linux-native
#   ./run-linux.sh --config raft/rack_config.linux.yml
#   ./run-linux.sh -- --any-extra-raft-arg      # pass the rest through to the suite

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${ROOT_DIR}"

RACK="rack1"
SLOT="linux-native"
CONFIG="raft/rack_config.linux.yml"
PASS=()

while [ $# -gt 0 ]; do
    case "$1" in
        --rack)     RACK="${2:?--rack needs a value}"; shift 2 ;;
        --slotName) SLOT="${2:?--slotName needs a value}"; shift 2 ;;
        --config)   CONFIG="${2:?--config needs a file}"; shift 2 ;;
        --)         shift; PASS+=("$@"); break ;;
        -h|--help)  awk '/^# run-linux.sh —/,/^#   \.\/run-linux.sh -- /' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *)          PASS+=("$1"); shift ;;
    esac
done

[ -f "${CONFIG}" ] || { echo "[run-linux] ERROR: rack config not found: ${CONFIG}" >&2; exit 1; }

# Ensure the framework + isolated host venv are installed (idempotent).
if [ ! -x "${ROOT_DIR}/python_venv/bin/python" ]; then
    echo "[run-linux] host venv missing — running install.sh"
    ./install.sh
fi
PY="${ROOT_DIR}/python_venv/bin/python"
[ -x "${PY}" ] || PY="python3"

echo "[run-linux] raft: --config ${CONFIG} --rack ${RACK} --slotName ${SLOT}"
exec "${PY}" raft/suites/test_rialto_conformance.py \
    --config "${CONFIG}" --rack "${RACK}" --slotName "${SLOT}" "${PASS[@]}"
