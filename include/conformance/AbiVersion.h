/*
 * Copyright 2026 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef RIALTO_CONFORMANCE_ABI_VERSION_H_
#define RIALTO_CONFORMANCE_ABI_VERSION_H_

#include <cstdint>

namespace rialto::conformance
{
/**
 * ABI-versioned conformance content (§4 — second versioning axis).
 *
 * The test content is tied to the platform-backend ABI version. A backend
 * declares its own version via rialtoPlatformBackendAbiVersion(); passing the
 * vN suite certifies that backend at vN. Bumps are ADDITIVE — a backend already
 * certified at vN is not re-certified by a later additive bump.
 *
 * History (the requirement each version entered at — coverage/matrix.yaml is
 * the authoritative record):
 *   v1  sinks / element registration
 *   v2  caps negotiation
 *   v3  video-master + playback-rate
 *   v4  audio fade / gap
 *   v5  audio codec switch (reattach)   <-- current
 */
constexpr uint32_t kPlatformBackendAbiVersion = 5;

/**
 * A case declares the ABI version it entered at. The runner skips a case whose
 * requiredAbi exceeds the version the target's backend reports, so an older
 * certified backend is never failed by a newer additive case.
 *
 * @param requiredAbi   ABI version the case was authored against.
 * @param targetAbi     ABI version the backend on the target reports.
 * @retval true if the case applies to a backend at targetAbi.
 */
constexpr bool abiApplies(uint32_t requiredAbi, uint32_t targetAbi)
{
    return requiredAbi <= targetAbi;
}

} // namespace rialto::conformance

#endif // RIALTO_CONFORMANCE_ABI_VERSION_H_
