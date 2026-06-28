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
 * executes every registered suite (group IDs are selective-run filters only) and
 * cleans up via UT_exit() on the success path.
 *
 * The same binary, with the same cases, runs on every target — only the
 * cross-compiler differs. The target's applicable cases are self-selected at
 * runtime from the capability gate (the KVP profile passed via -p).
 */

#include <ut.h>

int main(int argc, char **argv)
{
    UT_init(argc, argv);
    UT_run_tests();
    return 0;
}
