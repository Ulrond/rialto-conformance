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
# emulator.sh — stand the `linux-emulator` target up, and take it down again
# (issue #107). The target is the SC docker container running an sshd and the
# software Rialto; raft then treats it exactly as it treats a VM or a real box.
#
#   ./sc-build.sh                        # software Rialto + suite + package
#   ./emulator.sh up                     # the container becomes an ssh target
#   ./test.sh --slot linux-emulator      # a normal raft run against it
#   ./emulator.sh down
#
# Commands:
#   up      bring the target up and wait until it accepts logins
#   down    stop it (the container is --rm, so it disappears with the process)
#   status  is it up, and on which port
#   logs    follow the container's log
#
# The container runs with --net=host, so its sshd binds 127.0.0.1:2222 on THIS
# box — loopback only, never the network. Port 22 stays yours.
#
# Environment:
#   RIALTO_EMULATOR_SSH_PORT   sshd port (default 2222; match rack_config.yml)

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${ROOT_DIR}"

SSH_PORT="${RIALTO_EMULATOR_SSH_PORT:-2222}"
RUN_DIR="${ROOT_DIR}/build/emulator"
READY="${RUN_DIR}/.ready"
STOP="${RUN_DIR}/.stop"
LOG="${RUN_DIR}/container.log"
UP_TIMEOUT="${RIALTO_EMULATOR_UP_TIMEOUT:-600}"   # first run builds the image

usage() { awk '/^# emulator.sh —/,/^#   RIALTO_EMULATOR_SSH_PORT/' "$0" | sed 's/^# \{0,1\}//'; }

is_up() { [ -f "${READY}" ]; }

up() {
    if is_up; then
        echo "[emulator] already up (127.0.0.1:${SSH_PORT}) — nothing to do"
        return 0
    fi
    mkdir -p "${RUN_DIR}"
    rm -f "${STOP}" "${READY}"

    echo "[emulator] starting the target container (log: ${LOG})"
    RIALTO_EMULATOR_SSH_PORT="${SSH_PORT}" nohup "${ROOT_DIR}/docker/sc-exec.sh" \
        "RIALTO_EMULATOR_SSH_PORT=${SSH_PORT} ./docker/emulator-target-up.sh" \
        >"${LOG}" 2>&1 &
    local sc_pid=$!

    local waited=0
    while ! is_up; do
        if ! kill -0 "${sc_pid}" 2>/dev/null; then
            echo "[emulator] ERROR: the container exited before the target was ready." >&2
            echo "[emulator]        last lines of ${LOG}:" >&2
            tail -20 "${LOG}" >&2
            return 1
        fi
        if [ "${waited}" -ge "${UP_TIMEOUT}" ]; then
            echo "[emulator] ERROR: timed out after ${UP_TIMEOUT}s waiting for the target." >&2
            tail -20 "${LOG}" >&2
            return 1
        fi
        sleep 1
        waited=$((waited + 1))
    done

    echo "[emulator] up: ssh rialto@127.0.0.1 -p ${SSH_PORT} (key: ${RUN_DIR}/id_ed25519)"
    echo "[emulator] run the suite against it:  ./test.sh --slot linux-emulator"
}

down() {
    if [ ! -f "${LOG}" ] && ! is_up; then
        echo "[emulator] not up — nothing to do"
        return 0
    fi
    echo "[emulator] stopping the target container"
    mkdir -p "${RUN_DIR}"
    touch "${STOP}"
    local waited=0
    while is_up && [ "${waited}" -lt 30 ]; do sleep 1; waited=$((waited + 1)); done
    # The hold loop clears .ready on the way out. If it did not, the container is
    # wedged: name the container ourselves rather than leaving it running.
    if is_up; then
        echo "[emulator] hold loop did not exit — stopping the container directly"
        docker ps --filter "name=_${RIALTO_CONFORMANCE_IMAGE:-rialto-conformance-env}_" -q \
            | xargs -r docker stop >/dev/null
        rm -f "${READY}"
    fi
    rm -f "${STOP}"
    echo "[emulator] down"
}

status() {
    if is_up; then
        echo "[emulator] UP   — ssh rialto@127.0.0.1 -p ${SSH_PORT}"
        ss -tln 2>/dev/null | grep -q ":${SSH_PORT}\b" \
            || echo "[emulator] WARNING: nothing is listening on ${SSH_PORT} — try ./emulator.sh down"
    else
        echo "[emulator] DOWN — bring it up with ./emulator.sh up"
    fi
}

case "${1:-}" in
    up)          up ;;
    down)        down ;;
    status)      status ;;
    logs)        tail -f "${LOG}" ;;
    -h|--help|"") usage ;;
    *)           echo "[emulator] ERROR: unknown command '${1}'" >&2; usage >&2; exit 2 ;;
esac
