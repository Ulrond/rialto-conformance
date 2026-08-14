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
# docker. Container bootstrap (docker, sc, the env image) is docker/sc-exec.sh.
#
# This is the ENGINEER'S DEV LOOP on the software platform: build, launch the
# emulator, run — all in one container, with no ssh hop.
#
# For a formal run against a slot — emulator, VM or real box — use ./test.sh,
# which goes through raft and never builds (issue #104). The emulator slot is a
# container too: ./sc-build.sh, ./emulator.sh up, ./test.sh --slot linux-emulator.
#
# Usage:
#   ./sc-run.sh                       # build + run the CORE gate (default)
#   RIALTO_CONFORMANCE_TIER=all ./sc-run.sh
#   RIALTO_CONFORMANCE_SCOPE=L1 ./sc-run.sh    # one level (full | L1 | L2 | L3 | L4)
#   ./sc-run.sh -- "<custom command to run inside the container>"

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TIER="${RIALTO_CONFORMANCE_TIER:-core}"
SCOPE="${RIALTO_CONFORMANCE_SCOPE:-full}"

# Default in-container command: build the software Rialto + suite, bring up a
# RialtoServer, and run the gate — all in docker/run-in-container.sh.
DEFAULT_CMD="RIALTO_CONFORMANCE_TIER=${TIER} RIALTO_CONFORMANCE_SCOPE=${SCOPE} ./docker/run-in-container.sh"

RUN_CMD="${DEFAULT_CMD}"
if [ "${1:-}" = "--" ]; then shift; RUN_CMD="$*"; fi

exec "${ROOT_DIR}/docker/sc-exec.sh" "${RUN_CMD}"
