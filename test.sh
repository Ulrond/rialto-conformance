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
# test.sh — run the conformance suite against a target. THE entry point for
# testing, whatever the target is.
#
# Building and testing are separate (issue #104). This script NEVER builds: it
# ships the prebuilt package from build/dist that ./build.sh produced. If that
# package is missing, it stops and tells you to build.
#
# The target is only ever a rack slot. Emulator, Linux VM or real box run the
# same cases through the same python_raft flow — python_raft connects, deploys
# (or not), launches the target environment, runs the binary, pulls the xUnit
# back and adjudicates it. Point it somewhere else by changing --slotName, never
# by editing the tests.
#
# Usage:
#   ./test.sh                                   # default slot, full suite, core tier
#   ./test.sh --slot linux-native               # a different target
#   ./test.sh --scope L1                        # one level (L1|L2|L3|L4|full)
#   ./test.sh --tier all                        # core | extended | all
#   ./test.sh --slot lab-box-2 --scope L4 --tier core
#   ./test.sh --config raft/rack_config.yml --rack rack1 --slot reference-target
#   ./test.sh -- --any-extra-raft-arg           # rest passes through to raft

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${ROOT_DIR}"

RACK="rack1"
SLOT="${RIALTO_CONFORMANCE_SLOT:-linux-native}"
CONFIG="raft/rack_config.yml"
SCOPE="${RIALTO_CONFORMANCE_SCOPE:-full}"
TIER="${RIALTO_CONFORMANCE_TIER:-core}"
PASS=()

usage() { awk '/^# test.sh —/,/^#   \.\/test\.sh -- /' "$0" | sed 's/^# \{0,1\}//'; }

while [ $# -gt 0 ]; do
    case "$1" in
        --rack)              RACK="${2:?--rack needs a value}"; shift 2 ;;
        --slot|--slotName)   SLOT="${2:?--slot needs a value}"; shift 2 ;;
        --config)            CONFIG="${2:?--config needs a file}"; shift 2 ;;
        --scope)             SCOPE="${2:?--scope needs a value}"; shift 2 ;;
        --tier)              TIER="${2:?--tier needs a value}"; shift 2 ;;
        --)                  shift; PASS+=("$@"); break ;;
        -h|--help)           usage; exit 0 ;;
        *)                   PASS+=("$1"); shift ;;
    esac
done

case "${SCOPE}" in
    full|L1|L2|L3|L4) ;;
    *) echo "[test] ERROR: --scope must be one of full L1 L2 L3 L4 (got '${SCOPE}')" >&2; exit 1 ;;
esac
case "${TIER}" in
    core|extended|all) ;;
    *) echo "[test] ERROR: --tier must be one of core extended all (got '${TIER}')" >&2; exit 1 ;;
esac

[ -f "${CONFIG}" ] || { echo "[test] ERROR: rack config not found: ${CONFIG}" >&2; exit 1; }

# Testing does not build. Say so plainly rather than silently compiling.
if ! compgen -G "build/dist/rialto-conformance-*.tar.gz" >/dev/null; then
    echo "[test] ERROR: no package in build/dist — run ./build.sh first." >&2
    echo "[test]        (building is build.sh; this script only tests.)" >&2
    echo "[test]        If the target already has the suite installed, set" >&2
    echo "[test]        conformance.deploy: none for its slot in deviceConfig." >&2
    exit 1
fi

# Ensure the isolated host venv is present (idempotent; installs framework deps).
if [ ! -x "${ROOT_DIR}/python_venv/bin/python" ]; then
    echo "[test] host venv missing — running install.sh"
    ./install.sh
fi
PY="${ROOT_DIR}/python_venv/bin/python"
[ -x "${PY}" ] || PY="python3"

echo "[test] target: ${RACK}/${SLOT}  scope: ${SCOPE}  tier: ${TIER}"
export RIALTO_CONFORMANCE_SCOPE="${SCOPE}"
export RIALTO_CONFORMANCE_TIER="${TIER}"
# -u: python_raft reports a bad config with a print followed by os._exit(), which
# skips the flush — buffered, that diagnosis is lost and the run just exits 1.
exec "${PY}" -u raft/suites/test_rialto_conformance.py \
    --config "${CONFIG}" --rack "${RACK}" --slotName "${SLOT}" ${PASS[@]+"${PASS[@]}"}
