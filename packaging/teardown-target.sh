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
APP="${APP:-conformance}"
SOCK="/tmp/rialto-${APP}"
PID_FILE="${HERE}/.sim.pid"

# Deactivate the app first: the session server is the sim's child, and quitting
# the sim does not stop it. A server left running keeps ${SOCK} bound, and the
# next launch's server then cannot bind — so skipping this quietly breaks the
# NEXT run rather than this one.
curl -s -X POST -d "" "localhost:${SIM_PORT}/SetState/${APP}/NotRunning" >/dev/null 2>&1 || true
curl -s -X POST -d "" "localhost:${SIM_PORT}/Quit" >/dev/null 2>&1 || true

if [ -f "${PID_FILE}" ]; then
    SIM_PID="$(cat "${PID_FILE}" 2>/dev/null || true)"
    if [ -n "${SIM_PID}" ]; then
        kill "${SIM_PID}" 2>/dev/null || true
        wait "${SIM_PID}" 2>/dev/null || true
    fi
    rm -f "${PID_FILE}"
fi

# Whatever the orderly path missed. Our own processes only — a target running
# someone else's Rialto is not ours to stop.
pkill -u "$(id -u)" -x RialtoServer 2>/dev/null || true
for _ in $(seq 1 30); do pgrep -u "$(id -u)" -x RialtoServer >/dev/null 2>&1 || break; sleep 0.1; done
rm -f "${SOCK}"

rm -f "${HERE}/target-env.sh"
echo "[teardown] done"
