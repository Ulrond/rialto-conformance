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

#ifndef RIALTO_CONFORMANCE_RIALTO_RELEASE_H_
#define RIALTO_CONFORMANCE_RIALTO_RELEASE_H_

/**
 * @file RialtoRelease.h
 *
 * Rialto-release targeting (NOT Rialto's binary ABI — that is fixed per release).
 *
 * The suite is written against, and validated against, a specific Rialto release
 * — the one pinned in framework.lock. A requirement may declare the release it
 * applies from with CONFORMANCE_REQUIRE_SINCE("vX.Y.Z"); a target running an
 * older Rialto self-skips it (so a backend is never failed by a requirement for
 * an interface it predates). At a single pinned release the gate is dormant —
 * every requirement applies — and that is the normal case today.
 */

#include <cstdint>
#include <string>

namespace rialto::conformance
{
/**
 * The Rialto release the suite targets — kept in lockstep with the framework.lock
 * pin. This is the release the cases are written and validated against.
 */
constexpr const char *kTargetRialtoRelease = "v0.22.3";

/**
 * @brief Is release @p have at least release @p need?
 *
 * Compares dotted releases ("vA.B.C", a leading 'v' optional, missing components
 * treated as 0). Used to decide whether a requirement that applies from @p need
 * is in scope for the target's @p have.
 */
inline bool releaseAtLeast(const std::string &have, const std::string &need)
{
    auto parse = [](const std::string &s, int out[3]) {
        out[0] = out[1] = out[2] = 0;
        size_t i = (!s.empty() && (s[0] == 'v' || s[0] == 'V')) ? 1 : 0;
        int idx = 0;
        for (long n = 0; i <= s.size() && idx < 3; ++i)
        {
            if (i == s.size() || s[i] == '.')
            {
                out[idx++] = static_cast<int>(n);
                n = 0;
            }
            else if (s[i] >= '0' && s[i] <= '9')
            {
                n = n * 10 + (s[i] - '0');
            }
        }
    };
    int h[3], n[3];
    parse(have, h);
    parse(need, n);
    for (int k = 0; k < 3; ++k)
    {
        if (h[k] != n[k])
        {
            return h[k] > n[k];
        }
    }
    return true; // equal
}

} // namespace rialto::conformance

#endif // RIALTO_CONFORMANCE_RIALTO_RELEASE_H_
