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
# teardown-target.sh — stop what launch-target.sh started, ON THE TARGET.
#
# Named by the slot's `conformance.teardown`. Best-effort by design: raft runs it
# after the suite and never fails the run on its account, so a target left in an
# odd state reports as the test result it produced, not as a teardown error.

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SIM_PORT="${SIM_PORT:-9008}"
PID_FILE="${HERE}/.sim.pid"

curl -s -X POST -d "" "localhost:${SIM_PORT}/Quit" >/dev/null 2>&1 || true

if [ -f "${PID_FILE}" ]; then
    SIM_PID="$(cat "${PID_FILE}" 2>/dev/null || true)"
    if [ -n "${SIM_PID}" ]; then
        kill "${SIM_PID}" 2>/dev/null || true
        wait "${SIM_PID}" 2>/dev/null || true
    fi
    rm -f "${PID_FILE}"
fi

rm -f "${HERE}/target-env.sh"
echo "[teardown] done"
