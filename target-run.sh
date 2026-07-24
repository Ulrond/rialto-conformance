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
# Run the conformance gate against an EXTERNAL, already-running Rialto backend —
# a real target, or a render-capable x86 VM whose video sink actually renders
# (unlike the headless software platform, which fakes the render path, #18).
#
# This is the drop-in counterpart to sc-run.sh: sc-run.sh builds a *software*
# Rialto and stands up its own server; target-run.sh assumes the backend is
# already built and its server is already up, and simply builds the suite
# against it and runs the gate. Same binary, same cases — only the backend
# underneath differs, so any diff in the verdict is a real backend difference.
#
# The backend contract (one file):
#   A "session env" file that, when sourced, exports the running server socket
#   and the runtime library/plugin paths of the backend's real sinks:
#       RIALTO_SOCKET_PATH   path to the live RialtoServer session socket
#       LD_LIBRARY_PATH      the backend's libs (libRialtoClient, ...)
#       GST_PLUGIN_PATH      the backend's GStreamer sinks (rialtomse*sink)
#   and — to make the build one-command too — the install prefix to link:
#       RIALTO_NATIVE_PREFIX install prefix carrying lib/pkgconfig/RialtoClient.pc
#   (RIALTO_SINKS_RANK is set to 1 here if the backend did not.)
#
# Any backend that emits such a file is testable with a single command:
#   ./target-run.sh --session-env <file>
#
# Usage:
#   ./target-run.sh --session-env FILE [--rialto-prefix DIR]
#                   [--profile FILE] [--tier core|extended|all] [--no-build]
#
#   --session-env FILE   Backend session env to source (server socket + paths).
#                        Optional if those vars are already exported in your shell.
#   --rialto-prefix DIR  Build against this install prefix (lib/pkgconfig/
#                        RialtoClient.pc). Defaults to RIALTO_NATIVE_PREFIX from
#                        the sourced env; required if neither is set.
#   --profile FILE       HFP capability profile (default: profiles/hfp.linux.yaml).
#   --tier T             core (default) | extended | all.
#   --no-build           Reuse an existing build/bin/rialto_conformance.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${ROOT_DIR}"

SESSION_ENV=""
RIALTO_PREFIX="${RIALTO_NATIVE_PREFIX:-}"
PROFILE="profiles/hfp.linux.yaml"
TIER="${RIALTO_CONFORMANCE_TIER:-core}"
DO_BUILD=1

die() { echo "[target-run] ERROR: $*" >&2; exit 1; }

while [ $# -gt 0 ]; do
    case "$1" in
        --session-env)  SESSION_ENV="${2:?--session-env needs a file}"; shift 2 ;;
        --rialto-prefix) RIALTO_PREFIX="${2:?--rialto-prefix needs a dir}"; shift 2 ;;
        --profile)      PROFILE="${2:?--profile needs a file}"; shift 2 ;;
        --tier)         TIER="${2:?--tier needs a value}"; shift 2 ;;
        --no-build)     DO_BUILD=0; shift ;;
        -h|--help)      awk '/^# Run the conformance gate/,/^#   --no-build/' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *)              die "unknown argument: $1 (see --help)" ;;
    esac
done

# 1. Resolve the backend: source its session env, then validate the live server.
if [ -n "${SESSION_ENV}" ]; then
    [ -f "${SESSION_ENV}" ] || die "session env file not found: ${SESSION_ENV}"
    echo "[target-run] sourcing backend session env: ${SESSION_ENV}"
    # shellcheck disable=SC1090
    set -a; . "${SESSION_ENV}"; set +a
    # A prefix named in the env is the default build target unless overridden.
    RIALTO_PREFIX="${RIALTO_PREFIX:-${RIALTO_NATIVE_PREFIX:-}}"
fi

: "${RIALTO_SOCKET_PATH:?no RIALTO_SOCKET_PATH — pass --session-env or export it; is the backend server running?}"
[ -S "${RIALTO_SOCKET_PATH}" ] || die "no live server socket at RIALTO_SOCKET_PATH=${RIALTO_SOCKET_PATH} — start the backend's Rialto server first"
echo "[target-run] backend server socket: ${RIALTO_SOCKET_PATH}"

# 2. Build the suite against the backend's install prefix.
if [ "${DO_BUILD}" -eq 1 ]; then
    [ -n "${RIALTO_PREFIX}" ] || die "no install prefix to build against — pass --rialto-prefix or set RIALTO_NATIVE_PREFIX in the session env"
    [ -f "${RIALTO_PREFIX}/lib/pkgconfig/RialtoClient.pc" ] || \
        die "no RialtoClient.pc under ${RIALTO_PREFIX}/lib/pkgconfig — is this a Rialto install prefix?"
    echo "[target-run] building suite against ${RIALTO_PREFIX}"
    ./build.sh "RIALTO_NATIVE_PREFIX=${RIALTO_PREFIX}"
else
    echo "[target-run] --no-build: reusing build/bin/rialto_conformance"
fi
[ -x "${ROOT_DIR}/build/bin/rialto_conformance" ] || die "build/bin/rialto_conformance not found — drop --no-build to build it"

# 3. Runtime environment. Unlike the headless software platform, we do NOT promote
# fake sinks — the backend's real (rendering) sinks must be the ones exercised.
export LD_LIBRARY_PATH="${ROOT_DIR}/build/bin:${LD_LIBRARY_PATH:-}"
export RIALTO_SINKS_RANK="${RIALTO_SINKS_RANK:-1}"

# 4. Run the gate against the live backend. No ServerManagerSim is started (the
# backend owns the server); cases that need the sim control surface
# (control.stateToggle) self-skip when the HFP does not declare it.
[ -f "${PROFILE}" ] || die "HFP profile not found: ${PROFILE}"
echo "[target-run] running gate (tier=${TIER}, profile=${PROFILE}) against the live backend"
RIALTO_CONFORMANCE_TIER="${TIER}" ./build/bin/rialto_conformance -a -p "${PROFILE}"
