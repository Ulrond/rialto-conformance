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
# emulator-target-up.sh — turn this container into the `linux-emulator` TARGET,
# then hold it open (issue #107). Runs INSIDE the SC docker; the host side is
# ../emulator.sh, which backgrounds it and waits for the ready marker.
#
# The emulator is a target like any other: raft reaches it over ssh, deploys the
# package, runs the slot's launch command and pulls the xUnit back. Nothing about
# the run path is special-cased for it — that is the point of the slot.
#
# What it sets up, and why each piece is needed:
#
#   a login user   `rialto`, a FIXED name/uid so one static slot in
#                  rack_config.yml works for every engineer. `sc docker run` maps
#                  the CALLER as the in-container user, whose name differs per
#                  engineer, so the login user cannot be that one.
#   /opt/rialto    the software Rialto, copied out of the caller's checkout. The
#                  checkout is mode 0700 and owned by the caller, so `rialto`
#                  cannot read it; the copy is world-readable and sits at the
#                  first prefix packaging/launch-target.sh searches.
#   /opt/rialto-conformance
#                  the install dir raft deploys into, owned by `rialto`.
#   an sshd        python_raft has no `local` console type — every target is
#                  reached over ssh. `sc docker run` uses --net=host, so this
#                  sshd binds a LOOPBACK-ONLY port on the host, and both a
#                  host-side and an in-container raft can reach it. The host's
#                  own sshd keeps port 22; this one takes 2222 by default.
#
# Login is by key (raft's scp) and by password (paramiko, which python_raft's
# deviceManager only ever configures with a password). The password is a fixed
# lab value in rack_config.yml — safe because the sshd listens on 127.0.0.1 only
# and the container is disposable.
#
# Environment:
#   RIALTO_EMULATOR_SSH_PORT   sshd port (default 2222)
#   RIALTO_EMULATOR_USER       login user (default rialto; match rack_config.yml)

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

SSH_PORT="${RIALTO_EMULATOR_SSH_PORT:-2222}"
TARGET_USER="${RIALTO_EMULATOR_USER:-rialto}"
TARGET_UID=9002
TARGET_PASSWORD="rialto"                       # matches rack_config.yml; loopback-only
PREFIX_SRC="${ROOT_DIR}/framework/.native-install"
PREFIX="/opt/rialto"                           # launch-target.sh's first search path
INSTALL_DIR="/opt/rialto-conformance"          # device_config conformance.installDir

RUN_DIR="${ROOT_DIR}/build/emulator"           # host-visible (build/ is gitignored)
KEY="${RUN_DIR}/id_ed25519"
READY="${RUN_DIR}/.ready"
STOP="${RUN_DIR}/.stop"
SSHD_CONF="/etc/ssh/sshd_config_emulator"
SSHD_PID="/tmp/sshd-emulator.pid"

say() { echo "[emulator-target] $*"; }

# The software Rialto is built by ./sc-build.sh into the checkout. Without it
# there is no emulator to serve, so say that rather than failing later over ssh.
if [ ! -x "${PREFIX_SRC}/bin/RialtoServerManagerSim" ]; then
    say "ERROR: no software Rialto at ${PREFIX_SRC}"
    say "       run ./sc-build.sh first (it builds Rialto + the suite + the package)."
    exit 1
fi

mkdir -p "${RUN_DIR}"
rm -f "${READY}" "${STOP}"

# sudo resolves the container hostname on every call; without this each one warns.
grep -q " $(hostname)\$" /etc/hosts 2>/dev/null || \
    echo "127.0.0.1 $(hostname)" | sudo tee -a /etc/hosts >/dev/null

# --- login user -------------------------------------------------------------
if ! id -u "${TARGET_USER}" >/dev/null 2>&1; then
    say "creating login user ${TARGET_USER} (uid ${TARGET_UID})"
    sudo useradd --create-home --shell /bin/bash --uid "${TARGET_UID}" -o "${TARGET_USER}"
fi
echo "${TARGET_USER}:${TARGET_PASSWORD}" | sudo chpasswd
# raft matches the slot's `prompt` against the shell prompt, so pin it rather
# than inheriting whatever the distro skeleton sets.
echo "PS1='$ '" | sudo tee "/home/${TARGET_USER}/.bashrc_emulator" >/dev/null
sudo grep -q bashrc_emulator "/home/${TARGET_USER}/.bashrc" 2>/dev/null || \
    echo ". ~/.bashrc_emulator" | sudo tee -a "/home/${TARGET_USER}/.bashrc" >/dev/null

# --- the emulator, where the target user can read it ------------------------
say "installing the software Rialto into ${PREFIX}"
sudo rm -rf "${PREFIX}"
sudo mkdir -p "${PREFIX}"
sudo cp -a "${PREFIX_SRC}/." "${PREFIX}/"
sudo chmod -R a+rX "${PREFIX}"

# Register the libraries with the system loader, the way an installed Rialto sits
# on a real box. This is not belt-and-braces: the server manager spawns the
# session server with execve and an environment of its own, so the LD_LIBRARY_PATH
# the launch exports never reaches it. Without this the server cannot resolve
# libocdm / libWPEFrameworkCore / librdkgstreamerutils, exits before it logs
# anything (its stderr is /dev/null), and the app never reaches Active.
echo "${PREFIX}/lib" | sudo tee /etc/ld.so.conf.d/rialto-emulator.conf >/dev/null
sudo ldconfig

say "preparing ${INSTALL_DIR} for deployment"
sudo rm -rf "${INSTALL_DIR}"
sudo install -d -m 0755 -o "${TARGET_USER}" -g "${TARGET_USER}" "${INSTALL_DIR}"

# --- ssh key (raft's scp) ---------------------------------------------------
if [ ! -f "${KEY}" ]; then
    say "generating the slot's ssh key: ${KEY}"
    ssh-keygen -q -t ed25519 -N "" -C "rialto-conformance emulator slot" -f "${KEY}"
fi
chmod 0600 "${KEY}"
sudo install -d -m 0700 -o "${TARGET_USER}" -g "${TARGET_USER}" "/home/${TARGET_USER}/.ssh"
sudo install -m 0600 -o "${TARGET_USER}" -g "${TARGET_USER}" \
    "${KEY}.pub" "/home/${TARGET_USER}/.ssh/authorized_keys"

# --- sshd -------------------------------------------------------------------
sudo mkdir -p /run/sshd
sudo ssh-keygen -A >/dev/null
sudo tee "${SSHD_CONF}" >/dev/null <<EOF
Port ${SSH_PORT}
ListenAddress 127.0.0.1
Protocol 2
HostKey /etc/ssh/ssh_host_ed25519_key
HostKey /etc/ssh/ssh_host_rsa_key
PidFile ${SSHD_PID}
PermitRootLogin no
PubkeyAuthentication yes
PasswordAuthentication yes
AuthorizedKeysFile .ssh/authorized_keys
UsePAM no
X11Forwarding no
PrintMotd no
AcceptEnv LANG LC_*
Subsystem sftp /usr/lib/openssh/sftp-server
EOF

# A previous container may have left one behind (--net=host shares the port).
if [ -f "${SSHD_PID}" ]; then sudo kill "$(cat "${SSHD_PID}")" 2>/dev/null || true; sleep 1; fi

say "starting sshd on 127.0.0.1:${SSH_PORT}"
sudo /usr/sbin/sshd -f "${SSHD_CONF}"

# --- prove the hop before declaring the target ready ------------------------
SSH_OPTS=(-i "${KEY}" -p "${SSH_PORT}" -o BatchMode=yes -o StrictHostKeyChecking=no
          -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR)
for _ in $(seq 1 50); do
    if ssh "${SSH_OPTS[@]}" "${TARGET_USER}@127.0.0.1" true 2>/dev/null; then break; fi
    sleep 0.2
done
if ! ssh "${SSH_OPTS[@]}" "${TARGET_USER}@127.0.0.1" "test -x ${PREFIX}/bin/RialtoServerManagerSim && test -w ${INSTALL_DIR}"; then
    say "ERROR: ${TARGET_USER}@127.0.0.1:${SSH_PORT} is not usable as a target"
    exit 1
fi

say "target ready: ssh ${TARGET_USER}@127.0.0.1 -p ${SSH_PORT}, Rialto at ${PREFIX}"
touch "${READY}"

# --- hold the container open -------------------------------------------------
# `sc docker run` is foreground-only and --rm, so the container lives exactly as
# long as this script. The host's ./emulator.sh down drops the stop marker.
cleanup() {
    say "stopping sshd"
    [ -f "${SSHD_PID}" ] && sudo kill "$(cat "${SSHD_PID}")" 2>/dev/null || true
    rm -f "${READY}"
}
trap cleanup EXIT

say "holding the target open — stop it with ./emulator.sh down"
while [ ! -f "${STOP}" ]; do sleep 1; done
say "stop requested"
rm -f "${STOP}"
