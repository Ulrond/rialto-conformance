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
# sc-build.sh — build the Linux software platform + the suite inside the SC
# docker, and stop there. The build half of the emulator flow:
#
#   ./sc-build.sh                        # software Rialto + suite + package
#   ./emulator.sh up                     # the same container, now an ssh target
#   ./test.sh --slot linux-emulator      # raft runs against it
#   ./emulator.sh down
#
# It produces two things the emulator target needs:
#   framework/.native-install   the software Rialto (emulator.sh installs it as
#                               /opt/rialto inside the target container)
#   build/dist/*.tar.gz         the deployable package raft ships over ssh
#
# Building on the host is not supported for this platform — the toolchain and
# Rialto's native build deps live in the image, not on your box. Testing is
# ./test.sh and never builds (issue #104).
#
# `sc docker run` swallows the inner exit code — read the output, and check that
# build/dist holds a fresh tarball, rather than trusting this script's exit.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

BUILD_CMD="./install.sh && ./build-rialto.sh --no-deps && ./build.sh"

exec "${ROOT_DIR}/docker/sc-exec.sh" "${BUILD_CMD}"
