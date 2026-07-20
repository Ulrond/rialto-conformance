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
# Bundle the standalone installable artifact (§4 packaging model): the built
# binary + asset manifest + raft scripts. ut-raft installs this on the target and
# runs it against the already-installed Rialto. This packages — it does NOT build
# Rialto, and the artifact carries no Rialto internals (the binary links only the
# public client library, resolved on the target).
#
# NO capability profiles are bundled. The capability gate is the platform's HFP;
# it is platform-owned and fetched host-side, and ut-raft ships the resolved HFP
# to the target separately at run time (never baked into this artifact).
#
# Output: build/dist/rialto-conformance-<ut-core-tag>.tar.gz
# The LAST line printed is the artifact path (consumed by the raft adjudicator).

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${ROOT_DIR}/build/bin/rialto_conformance"
DIST_DIR="${ROOT_DIR}/build/dist"
STAGE_DIR="${DIST_DIR}/stage"

# Build if the binary is not present yet.
if [ ! -x "${BIN}" ]; then
    echo "[package.sh] binary not found, building first..." >&2
    "${ROOT_DIR}/build.sh" >&2
fi

UT_CORE_TAG="$(git -C "${ROOT_DIR}/framework/ut-core" describe --tags 2>/dev/null || echo unknown)"
ARTIFACT="${DIST_DIR}/rialto-conformance-${UT_CORE_TAG}.tar.gz"

rm -rf "${STAGE_DIR}"
mkdir -p "${STAGE_DIR}/raft" "${STAGE_DIR}/assets" "${DIST_DIR}"

# Binary + the ut-control runtime libs it loads (copied next to the binary by the
# ut-core build into build/bin).
cp "${BIN}" "${STAGE_DIR}/"
find "${ROOT_DIR}/build/bin" -maxdepth 1 -name '*.so*' -exec cp -a {} "${STAGE_DIR}/" \; 2>/dev/null || true

# Asset registry (real streams are fetched at run start, never bundled — §7.1).
cp -a "${ROOT_DIR}/assets/manifest.yaml" "${STAGE_DIR}/assets/"

# raft orchestration scripts so the package is self-describing on the target.
cp -a "${ROOT_DIR}/raft/." "${STAGE_DIR}/raft/"

# A run helper that sets LD_LIBRARY_PATH so the binary finds its co-located libs.
cat > "${STAGE_DIR}/run.sh" <<'EOF'
#!/usr/bin/env bash
# Run the conformance binary with co-located libs on the path.
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export LD_LIBRARY_PATH="${HERE}:${LD_LIBRARY_PATH:-}"
exec "${HERE}/rialto_conformance" "$@"
EOF
chmod +x "${STAGE_DIR}/run.sh"

tar -czf "${ARTIFACT}" -C "${STAGE_DIR}" .
echo "[package.sh] staged: ${STAGE_DIR}" >&2
echo "[package.sh] artifact:" >&2
echo "${ARTIFACT}"
