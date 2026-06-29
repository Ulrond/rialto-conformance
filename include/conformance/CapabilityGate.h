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

#ifndef RIALTO_CONFORMANCE_CAPABILITY_GATE_H_
#define RIALTO_CONFORMANCE_CAPABILITY_GATE_H_

/**
 * @file CapabilityGate.h
 *
 * Per-case applicability — the §3 req 2 capability gate. ONE binary, the same
 * cases on every platform; the target's configuration decides which apply. The
 * suite never branches on platform identity, and applicability is never a code
 * fork. It gates on PLATFORM FEATURES — what the target's platform supports /
 * exposes — not on SoC capability: a SoC may be capable of something the platform
 * built on it does not support, and two platforms on one SoC can expose different
 * feature sets, so the gate records platform support (the requirement the case
 * proves).
 *
 * Two sources, in priority order:
 *   1. End state  — the platform API reports the requirements it exposes
 *                   dynamically; the suite reads them at runtime.
 *   2. Fallback   — the ut-core KVP profile (deviceConfig.yaml, loaded with -p)
 *                   carries per-platform feature toggles. Read with
 *                   UT_KVP_PROFILE_GET_BOOL(); a case self-skips via
 *                   UT_IGNORE_TEST() when its feature is off.
 *
 * A case asks `CapabilityGate::requires("feature.key")` at the top of its body;
 * if unsupported the macro SKIPs (GTEST_SKIP) and the case is reported skipped,
 * not failed.
 */

#include <ut.h>
#include <ut_kvp_profile.h>
#include <ut_log.h>   // UT_LOG (ut.h's C++/gtest path does not pull it in)

#include <cstdint>
#include <string>

#include "conformance/RialtoRelease.h"

namespace rialto::conformance
{
/**
 * Root of the conformance capability gate within the deviceConfig KVP profile.
 *
 * The deviceConfig follows python_raft's shape — deviceConfig > cpe1 > platform
 * — and the gate is nested under the cpe at `rialto:`. A feature key such as
 * "codecs.video.av1" resolves to "deviceConfig/cpe1/rialto/codecs/video/av1".
 * One constant so the cpe/root convention lives in a single place.
 */
constexpr const char *kCapabilityRoot = "deviceConfig/cpe1/rialto";

/**
 * Resolves per-target feature applicability from the active capability source.
 *
 * The end-state dynamic reader is wired in here as backends gain it; until then
 * every lookup falls through to the deviceConfig KVP toggle of the same key, so
 * a case is authored once and never edited as a target moves from the fallback
 * bridge to dynamic reporting.
 */
class CapabilityGate
{
public:
    /**
     * @brief Is @p featureKey a feature this target's PLATFORM supports?
     *
     * Reads the deviceConfig capability boolean at
     * "deviceConfig/cpe1/rialto/<featureKey>". A missing key is treated as
     * unsupported, so a case only runs where the target has explicitly declared
     * the platform supports that variable feature. (Standard required features
     * are not gated — they are tested unconditionally.)
     *
     * @param featureKey  dot/slash sub-key under the gate root, e.g.
     *                     "drm.widevine" or "codecs.video.av1".
     * @retval true if the target's platform supports the feature.
     */
    static bool supports(const std::string &featureKey)
    {
        // End state hook: when the backend reports capabilities dynamically,
        // consult that first and return its verdict. Until a backend provides
        // it, fall through to the deviceConfig KVP toggle (the interim bridge).
        return readProfileBool(std::string{kCapabilityRoot} + "/" + featureKey);
    }

private:
    static bool readProfileBool(const std::string &fullKey)
    {
        if (ut_kvp_profile_getInstance() == nullptr)
        {
            return false; // no profile loaded (-p) => nothing declared supported
        }
        return UT_KVP_PROFILE_GET_BOOL(fullKey.c_str());
    }
};

} // namespace rialto::conformance

/**
 * @brief Gate the current case on a target capability.
 *
 * Place at the top of a case body. When the feature is not exposed by the
 * target, the case is SKIPPED (reported, not failed) via UT_IGNORE_TEST().
 *
 *   UT_ADD_TEST(WidevineTests, GenerateRequest) {
 *       CONFORMANCE_REQUIRE_CAP("drm.widevine");
 *       ... // only runs where the target declares Widevine
 *   }
 */
#define CONFORMANCE_REQUIRE_CAP(featureKey)                                                                            \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!::rialto::conformance::CapabilityGate::supports(featureKey))                                              \
        {                                                                                                              \
            UT_LOG("[capability-gate] '%s' not exposed by target; skipping case", featureKey);                        \
            UT_IGNORE_TEST();                                                                                          \
            return;                                                                                                    \
        }                                                                                                              \
    } while (0)

/**
 * @brief Gate the current case on the Rialto release a requirement applies from.
 *
 * Skips a case whose requirement was introduced in a Rialto release newer than
 * the one under test, so a backend is never failed by a requirement for an
 * interface it predates. The target release is the one the suite is built
 * against (kTargetRialtoRelease, the framework.lock pin). At a single pinned
 * release this is dormant — every requirement applies.
 *
 *   UT_ADD_TEST(SomeTests, NewFeature) {
 *       CONFORMANCE_REQUIRE_SINCE("v0.23.0");   // only where Rialto >= v0.23.0
 *       ...
 *   }
 */
#define CONFORMANCE_REQUIRE_SINCE(sinceRelease)                                                                        \
    do                                                                                                                 \
    {                                                                                                                  \
        const std::string target_{::rialto::conformance::kTargetRialtoRelease};                                       \
        if (!::rialto::conformance::releaseAtLeast(target_, (sinceRelease)))                                           \
        {                                                                                                              \
            UT_LOG("[release-gate] requirement needs Rialto %s, target is %s; skipping", (sinceRelease),              \
                   target_.c_str());                                                                                   \
            UT_IGNORE_TEST();                                                                                          \
            return;                                                                                                    \
        }                                                                                                              \
    } while (0)

#endif // RIALTO_CONFORMANCE_CAPABILITY_GATE_H_
