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

/**
 * @file main.cpp
 *
 * Entry point for the rialto-conformance test binary.
 *
 * In the ut-core C++ path (VARIANT=CPP / GoogleTest) every case registers itself
 * statically through UT_ADD_TEST / UT_ADD_TEST_TO_GROUP, so main only has to
 * stand the framework up and run. UT_init() parses the CLI (-a/-b, -p <profile>,
 * -e/-d <group>), loads the KVP profile, and selects the run mode; UT_run_tests()
 * executes every registered suite and cleans up via UT_exit() on the success path.
 *
 * Scope (RIALTO_CONFORMANCE_SCOPE) selects one level. It is applied per case as a
 * self-skip by ScopeGate.h, not as a GoogleTest filter — ut-core owns that flag
 * and overwrites it during UT_run_tests(); see ScopeGate.h for the detail. All
 * main() does is reject an unrecognised value up front, because silently running
 * the whole suite when one level was asked for would report far more coverage
 * than was actually exercised.
 *
 * The same binary, with the same cases, runs on every target — only the
 * cross-compiler differs. The target's applicable cases are self-selected at
 * runtime from the capability gate (the KVP profile passed via -p).
 */

#include "conformance/RegistrationProbe.h"
#include "conformance/ScopeGate.h"

#include <ut.h>

#include <cstdio>
#include <cstring>

namespace
{
/**
 * Reject an unrecognised RIALTO_CONFORMANCE_SCOPE before any case runs.
 */
bool scopeIsValid()
{
    const char *const scope = ::rialto::conformance::scopeName();
    for (const char *const valid : {"full", "L1", "L2", "L3", "L4"})
    {
        if (std::strcmp(scope, valid) == 0)
        {
            return true;
        }
    }
    std::fprintf(stderr,
                 "[conformance] ERROR: RIALTO_CONFORMANCE_SCOPE='%s' is not one of "
                 "full L1 L2 L3 L4\n",
                 scope);
    return false;
}
} // namespace

int main(int argc, char **argv)
{
    // Rank-gated element registration (RC-CORE-MSE-002) can only be observed in a
    // process that has not already registered the sinks. When the harness spawns
    // this binary as a registration probe, answer that and exit before the suite
    // runs; otherwise this is a no-op (returns -1) and the gate proceeds.
    if (const int probeResult = rialto::conformance::runRegistrationProbeIfRequested(); probeResult >= 0)
        return probeResult;

    if (!scopeIsValid())
        return 2;

    UT_init(argc, argv);
    UT_run_tests();
    return 0;
}
