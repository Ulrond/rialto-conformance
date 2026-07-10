#!/usr/bin/env bash
#
# Copyright 2026 RDK Management
# SPDX-License-Identifier: Apache-2.0
#
# SC-compatible container entrypoint. `sc docker run` invokes the image with
# LOCAL_USER_ID / LOCAL_USER_NAME / LOCAL_GROUP_ID (and bind-mounts your home),
# expecting the image to create that user and run the command as them so files
# created in the mounted workspace are owned by you, not root. This mirrors the
# comcast-sky core-ubuntu entrypoint convention (no corporate/internal bits).
#
# It also works under a plain `docker run` (the vars default sensibly).
set -Eeuo pipefail

USER_NAME="${LOCAL_USER_NAME:-user}"
USER_ID="${LOCAL_USER_ID:-9001}"
GROUP_ID="${LOCAL_GROUP_ID:-9001}"
START_DIR="${LOCAL_START_DIR:-/home/${USER_NAME}}"

groupadd -fg "${GROUP_ID}" local
useradd --shell /bin/bash -u "${USER_ID}" -g "${GROUP_ID}" -d "/home/${USER_NAME}" -o -c "" -m "${USER_NAME}" 2>/dev/null || true
export HOME="/home/${USER_NAME}"

# Optional docker group passthrough (sc passes LOCAL_DOCKER_GROUP).
if [[ -n "${LOCAL_DOCKER_GROUP:-}" ]]; then
    groupadd -fg "${LOCAL_DOCKER_GROUP}" docker || true
    usermod -a -G docker "${USER_NAME}" || true
fi

# Make the mapped user a passwordless sudoer (build deps install / --no-deps both
# fine, but this keeps parity with SC dev images).
echo "${USER_NAME} ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/${USER_NAME} && chmod 0440 /etc/sudoers.d/${USER_NAME}

echo "source /usr/local/bin/bashext.sh" >> "${HOME}/.bashrc" 2>/dev/null || true

# Source any env, then run any scripts, dropped into /etc/entrypoint.d.
shopt -s nullglob
for env in /etc/entrypoint.d/*.env; do set -a && . "${env}" && set +a; done
for ep in /etc/entrypoint.d/*.sh;  do [[ -x "${ep}" ]] && "${ep}"; done
shopt -u nullglob

cd "${START_DIR}" 2>/dev/null || cd "${HOME}"

# Run the requested command as the mapped user (gosu drops privileges).
exec /usr/local/bin/gosu "${USER_NAME}" /bin/bash -lc "$*"
