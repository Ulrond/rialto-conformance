#!/usr/bin/env bash
#
# Copyright 2026 RDK Management
# SPDX-License-Identifier: Apache-2.0
#
# In-container entry for the Linux software platform: build the suite + the
# software Rialto, bring up a RialtoServer, run the conformance gate, tear down.
# Runs inside the SC docker (cwd = the mounted repo); invoked by sc-run.sh.
#
# Surface B (native client API) is IPC-based: the client connects to a
# RialtoServer over RIALTO_SOCKET_PATH. We stand one up via the ServerManagerSim
# (an HTTP control surface on :9008): POST /SetState/<app>/Active with a socket
# name launches a RialtoServer SessionServer on /tmp/<socket>; the client then
# connects there. Surface A (sinks) only needs RIALTO_SINKS_RANK.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

PREFIX="${ROOT_DIR}/framework/.native-install"
TIER="${RIALTO_CONFORMANCE_TIER:-core}"
APP="conformance"
SOCK_NAME="rialto-${APP}"          # POST data: the sim maps "name" -> /tmp/name
SOCK="/tmp/${SOCK_NAME}"
SIM_PORT=9008
SIM_LOG="/tmp/rialto-sim.log"

# 1. Build (incremental on re-runs).
./install.sh
./build-rialto.sh --no-deps
./build.sh

# Runtime library + plugin paths for everything below.
export LD_LIBRARY_PATH="${ROOT_DIR}/build/bin:${PREFIX}/lib:${LD_LIBRARY_PATH:-}"
export GST_PLUGIN_PATH="${PREFIX}/lib/gstreamer-1.0:${ROOT_DIR}/framework/rialto-gstreamer/build"
export RIALTO_SINKS_RANK=1

# 2. Bring up the software RialtoServer via the ServerManagerSim.
export RIALTO_SESSION_SERVER_PATH="${PREFIX}/bin/RialtoServer"
echo "[run] starting RialtoServerManagerSim (HTTP :${SIM_PORT})"
"${PREFIX}/bin/RialtoServerManagerSim" > "${SIM_LOG}" 2>&1 &
SIM_PID=$!

cleanup() {
    curl -s -X POST -d "" "localhost:${SIM_PORT}/Quit" >/dev/null 2>&1 || true
    kill "${SIM_PID}" 2>/dev/null || true
    wait "${SIM_PID}" 2>/dev/null || true
}
trap cleanup EXIT

wait_for() {  # wait_for <description> <test-command...>
    local desc="$1"; shift
    for _ in $(seq 1 100); do "$@" >/dev/null 2>&1 && return 0; sleep 0.1; done
    echo "[run] ERROR: timed out waiting for ${desc}" >&2
    return 1
}

# HTTP control surface up?
if ! wait_for "ServerManagerSim HTTP" curl -sf "localhost:${SIM_PORT}/GetState/${APP}"; then
    echo "[run] --- sim log ---" >&2; cat "${SIM_LOG}" >&2; exit 1
fi

# Activate the app -> launches a RialtoServer on ${SOCK} (POST data = socket name).
echo "[run] activating app '${APP}' on socket ${SOCK}"
curl -s -X POST -d "${SOCK_NAME}" "localhost:${SIM_PORT}/SetState/${APP}/Active" || true

# Session-server socket present?
if ! wait_for "session-server socket ${SOCK}" test -S "${SOCK}"; then
    echo "[run] state: $(curl -s localhost:${SIM_PORT}/GetState/${APP} 2>/dev/null)" >&2
    echo "[run] appinfo: $(curl -s localhost:${SIM_PORT}/GetAppInfo/${APP} 2>/dev/null)" >&2
    echo "[run] /tmp sockets: $(ls -1 /tmp/*rialto* /tmp/*.sock /tmp/conformance 2>/dev/null | tr '\n' ' ')" >&2
    echo "[run] --- sim log ---" >&2; cat "${SIM_LOG}" >&2; exit 1
fi
echo "[run] RialtoServer up on ${SOCK}"

# 3. Run the gate against the live server.
export RIALTO_SOCKET_PATH="${SOCK}"
echo "[run] running CORE gate (tier=${TIER})"
RIALTO_CONFORMANCE_TIER="${TIER}" ./build/bin/rialto_conformance -a -p profiles/deviceConfig.linux.yaml
