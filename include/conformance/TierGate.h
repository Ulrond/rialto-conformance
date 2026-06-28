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

#ifndef RIALTO_CONFORMANCE_TIER_GATE_H_
#define RIALTO_CONFORMANCE_TIER_GATE_H_

/**
 * @file TierGate.h
 *
 * Tier selection — the CORE / EXTENDED axis, orthogonal to the L1-L4 level groups.
 *
 *   CORE     — interface conformance, derived from the Rialto external interface
 *              contract itself (coverage/rc-core-catalog.yaml). The drop-in /
 *              transform-safety gate: a new Rialto must uphold the same external
 *              contract as the old one. Run this first; it must be green.
 *   EXTENDED — app/player-requirement conformance, layered on top.
 *
 * ut-core's group ids (UT_TESTS_L1..L4) are the LEVEL axis and are owned by
 * ut-core; tier is a SECOND axis the suite selects at runtime by self-skip — the
 * same idiom as CapabilityGate / the ABI gate (UT_IGNORE_TEST() == GTEST_SKIP()).
 * Because tier gating is an in-test skip, it composes with the ut-core level
 * filter (`-e UT_TESTS_L1`) without both contending for the GoogleTest filter.
 *
 * Selection is read once from the environment variable RIALTO_CONFORMANCE_TIER:
 *   "core"      run only CORE cases (the transform-safety gate)
 *   "extended"  run only EXTENDED cases
 *   "all" | unset | anything else   run both (default)
 *
 * A case declares its tier at the top of its body, exactly like it declares a
 * capability or ABI requirement:
 *   UT_ADD_TEST(L1CapabilitiesTests, ...) { CONFORMANCE_CORE_TEST(); ... }
 */

#include <ut.h>

#include <cstdlib>
#include <cstring>

namespace rialto::conformance
{
enum class Tier
{
    Core,
    Extended,
};

/**
 * The active tier selection, parsed once from RIALTO_CONFORMANCE_TIER.
 * Returns whether @p tier should run under the current selection.
 */
inline bool tierSelected(Tier tier)
{
    // Parse the env var exactly once; default (unset/unrecognised) is "run both".
    static const int selection = [] {
        const char *env = std::getenv("RIALTO_CONFORMANCE_TIER");
        if (env == nullptr)
        {
            return 0; // all
        }
        if (std::strcmp(env, "core") == 0)
        {
            return 1; // core only
        }
        if (std::strcmp(env, "extended") == 0)
        {
            return 2; // extended only
        }
        return 0; // "all" or anything else
    }();

    switch (selection)
    {
    case 1:
        return tier == Tier::Core;
    case 2:
        return tier == Tier::Extended;
    default:
        return true;
    }
}

} // namespace rialto::conformance

/**
 * @brief Declare the current case as a CORE (interface-conformance) case.
 *
 * Skips when the run selects EXTENDED-only. Place at the top of the case body.
 */
#define CONFORMANCE_CORE_TEST()                                                                                         \
    do                                                                                                                  \
    {                                                                                                                   \
        if (!::rialto::conformance::tierSelected(::rialto::conformance::Tier::Core))                                    \
        {                                                                                                               \
            UT_LOG("[tier-gate] CORE case skipped (RIALTO_CONFORMANCE_TIER selects EXTENDED only)");                    \
            UT_IGNORE_TEST();                                                                                           \
            return;                                                                                                     \
        }                                                                                                               \
    } while (0)

/**
 * @brief Declare the current case as an EXTENDED (app-requirement) case.
 *
 * Skips when the run selects CORE-only (the transform-safety gate).
 */
#define CONFORMANCE_EXTENDED_TEST()                                                                                     \
    do                                                                                                                  \
    {                                                                                                                   \
        if (!::rialto::conformance::tierSelected(::rialto::conformance::Tier::Extended))                                \
        {                                                                                                               \
            UT_LOG("[tier-gate] EXTENDED case skipped (RIALTO_CONFORMANCE_TIER selects CORE only)");                    \
            UT_IGNORE_TEST();                                                                                           \
            return;                                                                                                     \
        }                                                                                                               \
    } while (0)

#endif // RIALTO_CONFORMANCE_TIER_GATE_H_
