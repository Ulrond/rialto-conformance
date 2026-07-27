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
 * @file FactoryTests.cpp
 *
 * L1 — function testing for Firebolt interface: the public factory link surface.
 *
 * Every native client object is reached through a published createFactory().
 * These cases prove the whole public factory surface is linkable and returns
 * usable factories (the canary that the binary links ONLY Rialto's public client
 * library + headers — libRialtoClient, the firebolt::rialto public API — never
 * internals), and that a create* method honours its documented null-on-error
 * contract rather than handing back a partially-constructed object.
 *
 * Coverage trace: coverage/matrix.yaml / rc-core-catalog.yaml rows
 * RC-CORE-FACTORY-001 (every public createFactory() is non-null),
 * RC-CORE-FACTORY-002 (create* returns null when construction cannot succeed).
 */

#include <ut.h>

#include "conformance/Surfaces.h"
#include "conformance/TierGate.h"

#include "IControl.h"
#include "IMediaKeys.h"
#include "IMediaKeysCapabilities.h"
#include "IMediaPipeline.h"
#include "IMediaPipelineCapabilities.h"
#include "IWebAudioPlayer.h"

#include <memory>
#include <string>

using namespace firebolt::rialto;
using rialto::conformance::NativeClientSurface;

namespace
{
// A key system no DRM backend supports; createMediaKeys must reject it rather
// than return a partially-constructed object (RC-CORE-FACTORY-002).
constexpr const char *kUnsupportedKeySystem = "com.example.nonexistent.keysystem";

class L1FactoryTests : public NativeClientSurface
{
};
} // namespace

UT_ADD_TEST_TO_GROUP(L1FactoryTests, UT_TESTS_L1);

/**
 * RC-CORE-FACTORY-001 — every public createFactory() returns a non-null factory.
 * These are the six published entry points to the native client API.
 */
UT_ADD_TEST(L1FactoryTests, EveryPublicFactoryIsNonNull)
{
    CONFORMANCE_CORE_TEST();

    UT_ASSERT_NOT_NULL(IControlFactory::createFactory().get());
    UT_ASSERT_NOT_NULL(IMediaPipelineFactory::createFactory().get());
    UT_ASSERT_NOT_NULL(IMediaPipelineCapabilitiesFactory::createFactory().get());
    UT_ASSERT_NOT_NULL(IMediaKeysFactory::createFactory().get());
    UT_ASSERT_NOT_NULL(IMediaKeysCapabilitiesFactory::createFactory().get());
    UT_ASSERT_NOT_NULL(IWebAudioPlayerFactory::createFactory().get());
}

/**
 * RC-CORE-FACTORY-002 — a create* method returns null (not a partially-
 * constructed object) when construction cannot succeed. Exercised via the
 * documented null-on-error path with a published contract that is independent of
 * the asynchronous backend: IMediaKeysFactory::createMediaKeys() with a key
 * system no DRM backend supports must return null.
 */
UT_ADD_TEST(L1FactoryTests, CreateMediaKeysReturnsNullForUnsupportedKeySystem)
{
    CONFORMANCE_CORE_TEST();

    auto factory = IMediaKeysFactory::createFactory();
    UT_ASSERT_NOT_NULL_FATAL(factory.get());

    std::unique_ptr<IMediaKeys> mediaKeys = factory->createMediaKeys(kUnsupportedKeySystem);
    UT_ASSERT_NULL(mediaKeys.get());
}
