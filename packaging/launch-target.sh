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
# launch-target.sh — bring the Rialto software platform up ON THE TARGET.
#
# Ships inside the conformance package and is named by the slot's
# `conformance.launch` in deviceConfig. raft just runs it; everything
# platform-specific lives here, so raft keeps no concept of the platform.
#
# It starts RialtoServerManagerSim, activates the conformance app, waits for the
# session-server socket, then writes the resolved environment the suite binary
# needs to `target-env.sh` — raft sources that file for the run, because each
# console command is its own shell and exports cannot cross between them.
#
# A target that already has Rialto running (a real box) does not need this: leave
# `conformance.launch` empty for that slot.
#
# Environment:
#   RIALTO_PREFIX   install prefix holding bin/RialtoServerManagerSim + lib.
#                   Defaults to /opt/rialto; the slot can override inline, e.g.
#                     launch: "RIALTO_PREFIX=/work/framework/.native-install ./launch-target.sh"
#   SIM_PORT        ServerManagerSim HTTP port (default 9008)
#   APP             app name to activate (default "conformance")
#   GST_PLUGIN_FEATURE_RANK
#                   passed through to target-env.sh when set, so a platform that
#                   needs to re-rank GStreamer elements (a headless target
#                   ranking the fake sinks, say) applies the same ranking to the
#                   server it starts and to the suite process that follows.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SIM_PORT="${SIM_PORT:-9008}"
APP="${APP:-conformance}"
SOCK_NAME="rialto-${APP}"          # POST data: the sim maps "name" -> /tmp/name
SOCK="/tmp/${SOCK_NAME}"
SIM_LOG="/tmp/rialto-sim.log"
PID_FILE="${HERE}/.sim.pid"
ENV_FILE="${HERE}/target-env.sh"

# Locate the Rialto install. RIALTO_PREFIX wins; otherwise try the usual places.
PREFIX=""
for candidate in "${RIALTO_PREFIX:-}" /opt/rialto /usr/local /usr; do
    [ -n "${candidate}" ] || continue
    if [ -x "${candidate}/bin/RialtoServerManagerSim" ]; then PREFIX="${candidate}"; break; fi
done
if [ -z "${PREFIX}" ]; then
    echo "[launch] ERROR: RialtoServerManagerSim not found." >&2
    echo "[launch]        Looked in: \${RIALTO_PREFIX} /opt/rialto /usr/local /usr" >&2
    echo "[launch]        Set RIALTO_PREFIX inline on this slot's conformance.launch, e.g." >&2
    echo "[launch]          launch: \"RIALTO_PREFIX=/opt/rialto ./launch-target.sh\"" >&2
    exit 1
fi
echo "[launch] Rialto prefix: ${PREFIX}"

export LD_LIBRARY_PATH="${HERE}:${PREFIX}/lib:${LD_LIBRARY_PATH:-}"
export GST_PLUGIN_PATH="${PREFIX}/lib/gstreamer-1.0:${GST_PLUGIN_PATH:-}"
export RIALTO_SINKS_RANK=1
export RIALTO_SESSION_SERVER_PATH="${PREFIX}/bin/RialtoServer"
export GST_PLUGIN_FEATURE_RANK="${GST_PLUGIN_FEATURE_RANK:-}"

wait_for() {  # wait_for <description> <test-command...>
    local desc="$1"; shift
    for _ in $(seq 1 100); do "$@" >/dev/null 2>&1 && return 0; sleep 0.1; done
    echo "[launch] ERROR: timed out waiting for ${desc}" >&2
    return 1
}

# Clear what a previous run left behind. A session server outlives the sim that
# spawned it, and it keeps the socket bound: the server this launch starts then
# cannot bind and exits, while the leftover socket FILE makes the readiness check
# below pass instantly. The result is a launch that reports success over a dead
# server, and a suite run that blames the platform for it. Only ever our own
# processes — a target running someone else's Rialto is not ours to stop.
if [ -S "${SOCK}" ] || pgrep -u "$(id -u)" -x RialtoServer >/dev/null 2>&1; then
    echo "[launch] clearing a previous session server and its socket"
    curl -s -X POST -d "" "localhost:${SIM_PORT}/SetState/${APP}/NotRunning" >/dev/null 2>&1 || true
    pkill -u "$(id -u)" -x RialtoServer 2>/dev/null || true
    for _ in $(seq 1 30); do pgrep -u "$(id -u)" -x RialtoServer >/dev/null 2>&1 || break; sleep 0.1; done
    rm -f "${SOCK}"
fi

echo "[launch] starting RialtoServerManagerSim (HTTP :${SIM_PORT})"
"${PREFIX}/bin/RialtoServerManagerSim" > "${SIM_LOG}" 2>&1 &
echo $! > "${PID_FILE}"

if ! wait_for "ServerManagerSim HTTP" curl -sf "localhost:${SIM_PORT}/GetState/${APP}"; then
    echo "[launch] --- sim log ---" >&2; cat "${SIM_LOG}" >&2; exit 1
fi

echo "[launch] activating app '${APP}' on socket ${SOCK}"
curl -s -X POST -d "${SOCK_NAME}" "localhost:${SIM_PORT}/SetState/${APP}/Active" || true

if ! wait_for "session-server socket ${SOCK}" test -S "${SOCK}"; then
    echo "[launch] state: $(curl -s localhost:${SIM_PORT}/GetState/${APP} 2>/dev/null)" >&2
    echo "[launch] --- sim log ---" >&2; cat "${SIM_LOG}" >&2; exit 1
fi
echo "[launch] RialtoServer up on ${SOCK}"

echo "[launch] waiting for app '${APP}' to reach Active (RUNNING)"
for _ in $(seq 1 100); do
    if curl -s "localhost:${SIM_PORT}/GetState/${APP}" 2>/dev/null | grep -q "returned: Active"; then
        echo "[launch] app '${APP}' is Active (RUNNING)"; break
    fi
    curl -s -X POST -d "" "localhost:${SIM_PORT}/SetState/${APP}/Active" >/dev/null 2>&1 || true
    sleep 0.2
done
if ! curl -s "localhost:${SIM_PORT}/GetState/${APP}" 2>/dev/null | grep -q "returned: Active"; then
    # Fatal, not a warning: the session server is where every native-surface case
    # connects. Running the suite against one that never came up produces a page
    # of conformance failures that are nothing of the sort.
    echo "[launch] ERROR: app '${APP}' did not reach Active; state: $(curl -s localhost:${SIM_PORT}/GetState/${APP} 2>/dev/null)" >&2
    # The commonest cause, and an invisible one: the server manager spawns the
    # session server with execve and an environment of its own, so nothing this
    # script exports reaches it — and the server's stderr goes to /dev/null, so a
    # loader failure leaves no trace anywhere. Name it explicitly.
    if command -v ldd >/dev/null 2>&1; then
        MISSING="$(env -u LD_LIBRARY_PATH ldd "${PREFIX}/bin/RialtoServer" 2>/dev/null | grep "not found" || true)"
        if [ -n "${MISSING}" ]; then
            echo "[launch] the session server cannot resolve these without LD_LIBRARY_PATH:" >&2
            echo "${MISSING}" | sed 's/^/[launch]   /' >&2
            echo "[launch] it is spawned with its own environment, so ${PREFIX}/lib must be" >&2
            echo "[launch] on the system loader path — add it to /etc/ld.so.conf.d and run ldconfig." >&2
        fi
    fi
    echo "[launch] --- sim log ---" >&2; cat "${SIM_LOG}" >&2
    exit 1
fi

# Hand the resolved environment to the run — raft sources this (conformance.envFile).
cat > "${ENV_FILE}" <<EOF
export LD_LIBRARY_PATH="${LD_LIBRARY_PATH}"
export GST_PLUGIN_PATH="${GST_PLUGIN_PATH}"
export RIALTO_SINKS_RANK="${RIALTO_SINKS_RANK}"
export RIALTO_SESSION_SERVER_PATH="${RIALTO_SESSION_SERVER_PATH}"
export RIALTO_SOCKET_PATH="${SOCK}"
export RIALTO_CONFORMANCE_SIM_HOST="localhost"
export RIALTO_CONFORMANCE_SIM_PORT="${SIM_PORT}"
export RIALTO_CONFORMANCE_APP="${APP}"
EOF
# Only when the platform asked for a ranking — an empty value would blank out a
# ranking the target's own environment already carries.
if [ -n "${GST_PLUGIN_FEATURE_RANK}" ]; then
    echo "export GST_PLUGIN_FEATURE_RANK=\"${GST_PLUGIN_FEATURE_RANK}\"" >> "${ENV_FILE}"
fi
echo "[launch] wrote ${ENV_FILE}"
