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

# Software render path (issue #18): the RialtoServer decodes through a GStreamer
# playbin that leaves audio-sink/video-sink unset, so it falls to autoaudiosink/
# autovideosink. This container is headless (no display, no audio device), so
# promote the fake sinks to the top rank — autodetect then selects them and real
# decode (gstreamer1.0-libav: avdec_h264/avdec_aac) runs to EOS without hardware.
# Exported here, before the sim launches, so the spawned RialtoServer inherits it.
export GST_PLUGIN_FEATURE_RANK="fakevideosink:MAX,fakeaudiosink:MAX${GST_PLUGIN_FEATURE_RANK:+,${GST_PLUGIN_FEATURE_RANK}}"

# Assert the image can actually decode+render headlessly before running the gate;
# fail loudly if the software render path is broken (data-path cases depend on it).
"${ROOT_DIR}/docker/verify-render.sh"

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

# The socket exists as soon as the session server starts listening, but the app
# reaches the RUNNING application state (what IControl clients are notified of)
# only once the server completes switchToActive. Confirm Active before running
# the gate: a registering control client is then handed the current RUNNING
# state on registration (ControlService::addControl replays it), which the
# IControl state-notification case relies on. Poll GetState, re-issuing
# SetState/Active (the changeSessionServerState path) so activation is driven
# deterministically even if the initial initiateApplication landed before the
# server's IPC was ready. Best-effort: a timeout warns and proceeds rather than
# failing the cases that do not need RUNNING.
echo "[run] waiting for app '${APP}' to reach Active (RUNNING)"
for _ in $(seq 1 100); do
    if curl -s "localhost:${SIM_PORT}/GetState/${APP}" 2>/dev/null | grep -q "returned: Active"; then
        echo "[run] app '${APP}' is Active (RUNNING)"; break
    fi
    curl -s -X POST -d "" "localhost:${SIM_PORT}/SetState/${APP}/Active" >/dev/null 2>&1 || true
    sleep 0.2
done
if ! curl -s "localhost:${SIM_PORT}/GetState/${APP}" 2>/dev/null | grep -q "returned: Active"; then
    echo "[run] WARNING: app '${APP}' did not reach Active; state: $(curl -s localhost:${SIM_PORT}/GetState/${APP} 2>/dev/null)" >&2
fi

# 3. Run the gate against the live server.
export RIALTO_SOCKET_PATH="${SOCK}"
# Expose the sim control surface so a case can drive the server application-state
# machine after connecting (RC-CORE-CONTROL-002's notify-on-transition clause:
# SimControl POSTs SetState/<app>/{Inactive,Active}). Gated on the
# `control.stateToggle` deviceConfig feature (declared for linux-native only).
export RIALTO_CONFORMANCE_SIM_HOST="localhost"
export RIALTO_CONFORMANCE_SIM_PORT="${SIM_PORT}"
export RIALTO_CONFORMANCE_APP="${APP}"
echo "[run] running CORE gate (tier=${TIER})"
RIALTO_CONFORMANCE_TIER="${TIER}" ./build/bin/rialto_conformance -a -p profiles/deviceConfig.linux.yaml
