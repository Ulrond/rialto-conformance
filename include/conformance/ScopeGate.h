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

#ifndef RIALTO_CONFORMANCE_SCOPE_GATE_H_
#define RIALTO_CONFORMANCE_SCOPE_GATE_H_

/**
 * @file ScopeGate.h
 *
 * Scope selection — run one LEVEL (L1-L4) instead of the whole suite.
 *
 * Selection is read once from RIALTO_CONFORMANCE_SCOPE:
 *   "full" | unset   run every level (default)
 *   "L1".."L4"       run only that level
 *
 * ./test.sh --scope L1 sets it for a raft run; RIALTO_CONFORMANCE_SCOPE=L1
 * ./sc-run.sh sets it for the dev loop.
 *
 * **Why this is a self-skip and not a GoogleTest filter.** ut-core owns the
 * GoogleTest filter and will not share it. `-e/-d <group>` look like the right
 * mechanism — ut-core parses them and UT_get_test_filter() builds a correct
 * include list from UT_ADD_TEST_TO_GROUP — but in automated mode the filter is
 * never applied: runTests() is a bare RUN_ALL_TESTS(), and the computed filter
 * only marks suites active for the interactive console menu. Worse, the
 * UTTestRunner constructor ends with setTestFilter(formatPatterns(inactive)),
 * and formatPatterns({}) returns "-", so it unconditionally overwrites
 * GTEST_FLAG(filter) with "-" — clobbering anything main() set beforehand, with
 * no hook between construction and the run.
 *
 * So scope self-skips instead, exactly like TierGate and CapabilityGate
 * (UT_IGNORE_TEST() == GTEST_SKIP()). This also composes: a skipped case is
 * *reported* as skipped rather than silently absent, so a scoped run still shows
 * what it did not exercise — the same property that makes the capability gate
 * honest.
 *
 * The level of a case is its suite class name prefix, which every class already
 * carries by convention (L1CapsTests, L4DataProtocolTests, ...) and which
 * matches the ut-core group it registers into via UT_ADD_TEST_TO_GROUP.
 *
 * Invoked from the tier macros (CONFORMANCE_CORE_TEST / CONFORMANCE_EXTENDED_TEST),
 * which every case body already declares — so cases need no extra line.
 */

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

namespace rialto::conformance
{
/**
 * @brief Is the running case in the selected scope?
 *
 * @return true when the whole suite is selected, or when the current case's
 *         suite name carries the selected level prefix.
 */
inline bool scopeSelected()
{
    static const std::string selection = []
    {
        const char *const env = std::getenv("RIALTO_CONFORMANCE_SCOPE");
        return (env != nullptr && *env != '\0') ? std::string{env} : std::string{"full"};
    }();

    if (selection == "full")
    {
        return true;
    }

    const ::testing::TestInfo *const info = ::testing::UnitTest::GetInstance()->current_test_info();
    if (info == nullptr)
    {
        return true;
    }

    const std::string suiteName{info->test_suite_name()};
    return suiteName.rfind(selection, 0) == 0;
}

/**
 * @brief The configured scope, for logging.
 */
inline const char *scopeName()
{
    const char *const env = std::getenv("RIALTO_CONFORMANCE_SCOPE");
    return (env != nullptr && *env != '\0') ? env : "full";
}

} // namespace rialto::conformance

#endif // RIALTO_CONFORMANCE_SCOPE_GATE_H_
