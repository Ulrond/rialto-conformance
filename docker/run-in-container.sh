#!/usr/bin/env bash
#
# Copyright 2026 RDK Management
# SPDX-License-Identifier: Apache-2.0
#
# In-container entry for the Linux software platform — the ENGINEER'S DEV LOOP.
# Runs inside the SC docker (cwd = the mounted repo); invoked by sc-run.sh.
#
# Building and testing are separate (issue #104):
#
#   build   ./install.sh, ./build-rialto.sh, ./build.sh  (leaves the deployable
#           tarball in build/dist)
#   launch  packaging/launch-target.sh — the SAME script raft names as a slot's
#           conformance.launch, so the emulator is brought up here exactly as it
#           is on any other target
#   test    the packaged binary against the launched emulator, with the launch's
#           target-env.sh sourced — exactly what raft does after its ssh hop
#
# This is the tight iterate-and-debug loop, not a second test path: the launch
# script, the environment hand-off and the package are all the ones raft uses.
# For a formal run against a slot — emulator, VM or box — use ./test.sh.
#
# Firebolt interface (native client API) is IPC-based: the client connects to a
# RialtoServer over RIALTO_SOCKET_PATH. launch-target.sh stands one up via the
# ServerManagerSim (an HTTP control surface on :9008): POST /SetState/<app>/Active
# with a socket name launches a RialtoServer SessionServer on /tmp/<socket>; the
# client then connects there. mseSink interface (sinks) only needs
# RIALTO_SINKS_RANK.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

PREFIX="${ROOT_DIR}/framework/.native-install"
TIER="${RIALTO_CONFORMANCE_TIER:-core}"
SCOPE="${RIALTO_CONFORMANCE_SCOPE:-full}"
STAGE_DIR="${ROOT_DIR}/build/dist/stage"

# --- build -----------------------------------------------------------------
# Incremental on re-runs. build.sh also packages, so build/dist/stage holds the
# deployable layout (binary + libs + launch/teardown scripts).
./install.sh
./build-rialto.sh --no-deps
./build.sh

# The rialto sinks only register when RIALTO_SINKS_RANK is set: the plugin reads
# it in plugin_init and registers nothing without it (RialtoGSteamerPlugin.cpp).
# GStreamer then CACHES that empty result in ~/.cache/gstreamer-1.0 — which is on
# the mounted home, so it survives the container. Set it before ANY gst tool runs
# (verify-render.sh below is the first), or the render check poisons the registry
# and every mseSink case fails with a NULL factory.
export RIALTO_SINKS_RANK=1

# Software render path (issue #18): the RialtoServer decodes through a GStreamer
# playbin that leaves audio-sink/video-sink unset, so it falls to autoaudiosink/
# autovideosink. This container is headless, so rank the fake sinks to MAX and
# verify the decode paths resolve before spending a run on them.
export GST_PLUGIN_FEATURE_RANK="fakevideosink:MAX,fakeaudiosink:MAX${GST_PLUGIN_FEATURE_RANK:+,${GST_PLUGIN_FEATURE_RANK}}"
LD_LIBRARY_PATH="${ROOT_DIR}/build/bin:${PREFIX}/lib:${LD_LIBRARY_PATH:-}" \
    GST_PLUGIN_PATH="${PREFIX}/lib/gstreamer-1.0:${GST_PLUGIN_PATH:-}" \
    "${ROOT_DIR}/docker/verify-render.sh"

# --- launch ----------------------------------------------------------------
# The same script raft runs on a target, against the locally built prefix.
cleanup() { RIALTO_PREFIX="${PREFIX}" "${STAGE_DIR}/teardown-target.sh" >/dev/null 2>&1 || true; }
trap cleanup EXIT

echo "[run] launching the software platform via packaging/launch-target.sh"
RIALTO_PREFIX="${PREFIX}" "${STAGE_DIR}/launch-target.sh"

# --- test ------------------------------------------------------------------
# Source the environment the launch resolved, exactly as raft does via envFile.
# The sinks come from the install prefix, which launch-target.sh already put on
# GST_PLUGIN_PATH — the rialto-gstreamer build tree is not needed here.
# shellcheck disable=SC1091
. "${STAGE_DIR}/target-env.sh"

# Scope goes to the binary as RIALTO_CONFORMANCE_SCOPE — ut-core's -e/-d are
# inert in automated mode, so src/main.cpp sets the GoogleTest filter instead.
# main.cpp rejects an unknown value rather than silently running everything.
echo "[run] running the gate (tier=${TIER} scope=${SCOPE})"
RIALTO_CONFORMANCE_TIER="${TIER}" RIALTO_CONFORMANCE_SCOPE="${SCOPE}" \
    "${STAGE_DIR}/rialto_conformance" -a -p profiles/hfp.linux.yaml
