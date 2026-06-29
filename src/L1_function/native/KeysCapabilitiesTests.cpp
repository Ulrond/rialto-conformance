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
 * @file KeysCapabilitiesTests.cpp
 *
 * L1 — function testing for Surface B: IMediaKeysCapabilities.
 *
 * CORE interface-conformance: the key-system capabilities query API must be
 * obtainable and internally consistent regardless of which DRM systems a given
 * platform provisions. The presence of a *specific* key system is a variable
 * platform feature and is gated (CONFORMANCE_REQUIRE_CAP); the API contract
 * itself is tested unconditionally.
 *
 * Coverage trace: coverage/matrix.yaml / rc-core-catalog.yaml rows
 * RC-CORE-KEYSCAP-001 (obtainable), RC-CORE-KEYSCAP-002 (supportsKeySystem
 * consistency + legal-query-of-any-string), RC-CORE-KEYSCAP-003 (version),
 * RC-CORE-KEYSCAP-004 (server certificate).
 */

#include <ut.h>

#include "conformance/AbiVersion.h"
#include "conformance/CapabilityGate.h"
#include "conformance/Surfaces.h"
#include "conformance/TierGate.h"

#include "IMediaKeysCapabilities.h"

#include <memory>
#include <string>
#include <vector>

using namespace firebolt::rialto;
using rialto::conformance::NativeClientSurface;

namespace
{
class L1KeysCapabilitiesTests : public NativeClientSurface
{
};

// Public W3C-EME key-system identifiers (not platform-specific test code — these
// are the standard strings the capabilities API is queried with).
constexpr const char *kWidevine = "com.widevine.alpha";
constexpr const char *kPlayReady = "com.microsoft.playready";
// A deliberately unsupported string: any string is a LEGAL query and must return
// false rather than error (RC-CORE-KEYSCAP-002).
constexpr const char *kUnsupported = "com.example.nonexistent.keysystem";

// Obtain the capabilities singleton. No fatal asserts here: GTest's fatal
// asserts (ASSERT_*) return void, so they cannot live in a value-returning
// helper. Returns nullptr on factory failure; callers assert non-null.
std::shared_ptr<IMediaKeysCapabilities> obtainCapabilities()
{
    auto factory = IMediaKeysCapabilitiesFactory::createFactory();
    return factory ? factory->getMediaKeysCapabilities() : nullptr;
}
} // namespace

UT_ADD_TEST_TO_GROUP(L1KeysCapabilitiesTests, UT_TESTS_L1);

/**
 * RC-CORE-KEYSCAP-001 — the capabilities singleton is obtainable and the
 * supported-key-systems query is callable (the list may legitimately be empty on
 * a platform that provisions no DRM; emptiness is not a failure of the contract).
 */
UT_ADD_TEST(L1KeysCapabilitiesTests, CapabilitiesObtainable)
{
    CONFORMANCE_CORE_TEST();

    auto caps = obtainCapabilities();
    UT_ASSERT_NOT_NULL_FATAL(caps.get());

    // Callable without error; we only assert it returns (content varies by target).
    const std::vector<std::string> systems = caps->getSupportedKeySystems();
    (void)systems;
}

/**
 * RC-CORE-KEYSCAP-002 — supportsKeySystem agrees with getSupportedKeySystems:
 * every advertised system is supported, any string is a legal query, and an
 * unsupported string returns false (not an error).
 */
UT_ADD_TEST(L1KeysCapabilitiesTests, SupportsKeySystemConsistentWithList)
{
    CONFORMANCE_CORE_TEST();

    auto caps = obtainCapabilities();
    UT_ASSERT_NOT_NULL_FATAL(caps.get());

    // Each advertised system must report as supported.
    for (const std::string &system : caps->getSupportedKeySystems())
    {
        UT_ASSERT_TRUE(caps->supportsKeySystem(system));
    }

    // An unsupported string is a legal query and must return false, not throw.
    UT_ASSERT_FALSE(caps->supportsKeySystem(kUnsupported));
}

/**
 * RC-CORE-KEYSCAP-003 — getSupportedKeySystemVersion writes a version and
 * returns true for each supported system; an unsupported system returns false.
 */
UT_ADD_TEST(L1KeysCapabilitiesTests, SupportedKeySystemVersionContract)
{
    CONFORMANCE_CORE_TEST();

    auto caps = obtainCapabilities();
    UT_ASSERT_NOT_NULL_FATAL(caps.get());

    for (const std::string &system : caps->getSupportedKeySystems())
    {
        std::string version;
        UT_ASSERT_TRUE(caps->getSupportedKeySystemVersion(system, version));
        UT_ASSERT_FALSE(version.empty());
    }

    std::string unusedVersion;
    UT_ASSERT_FALSE(caps->getSupportedKeySystemVersion(kUnsupported, unusedVersion));
}

/**
 * RC-CORE-KEYSCAP-004 — isServerCertificateSupported is a legal query for any
 * key system; an unsupported system reports false rather than erroring.
 */
UT_ADD_TEST(L1KeysCapabilitiesTests, ServerCertificateQueryIsLegal)
{
    CONFORMANCE_CORE_TEST();

    auto caps = obtainCapabilities();
    UT_ASSERT_NOT_NULL_FATAL(caps.get());

    UT_ASSERT_FALSE(caps->isServerCertificateSupported(kUnsupported));
}

/**
 * RC-CORE-KEYSCAP-002 (Widevine instance) — where the platform declares Widevine,
 * it must be advertised and report as supported. GATED on the variable feature
 * "drm.widevine"; a platform without Widevine self-skips.
 */
UT_ADD_TEST(L1KeysCapabilitiesTests, WidevineSupportedWhenDeclared)
{
    CONFORMANCE_CORE_TEST();
    CONFORMANCE_REQUIRE_CAP("drm.widevine");

    auto caps = obtainCapabilities();
    UT_ASSERT_NOT_NULL_FATAL(caps.get());

    UT_ASSERT_TRUE(caps->supportsKeySystem(kWidevine));
}

/**
 * RC-CORE-KEYSCAP-002 (PlayReady instance) — as above for PlayReady. GATED on
 * "drm.playready".
 */
UT_ADD_TEST(L1KeysCapabilitiesTests, PlayReadySupportedWhenDeclared)
{
    CONFORMANCE_CORE_TEST();
    CONFORMANCE_REQUIRE_CAP("drm.playready");

    auto caps = obtainCapabilities();
    UT_ASSERT_NOT_NULL_FATAL(caps.get());

    UT_ASSERT_TRUE(caps->supportsKeySystem(kPlayReady));
}
