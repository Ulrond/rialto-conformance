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
 * @file CapabilitiesTests.cpp
 *
 * L1 — function testing for Surface B: IMediaPipelineCapabilities.
 *
 * Each function exercised on its own against the published interface (§6 L1).
 * These cases also prove the Phase-0 link surface: the binary links ONLY
 * Rialto's public client library + headers (libRialtoClient, the firebolt::rialto
 * public API) — never Rialto internals.
 *
 * Coverage trace: coverage/matrix.yaml rows op:getSupportedMimeTypes / op:
 * isMimeTypeSupported / op:isVideoMaster (RC-FMT-001 format-support +
 * RC-AVSYNC-001 video-master requirements).
 */

#include <ut.h>

#include "conformance/AbiVersion.h"
#include "conformance/CapabilityGate.h"
#include "conformance/Surfaces.h"
#include "conformance/TierGate.h"

#include "IMediaPipelineCapabilities.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

using namespace firebolt::rialto;
using rialto::conformance::NativeClientSurface;

namespace
{
class L1CapabilitiesTests : public NativeClientSurface
{
};
} // namespace

UT_ADD_TEST_TO_GROUP(L1CapabilitiesTests, UT_TESTS_L1);

/**
 * createFactory() returns a usable factory, and the factory yields a concrete
 * IMediaPipelineCapabilities. This is the canary that the public client library
 * is linked and the published API is reachable.
 */
UT_ADD_TEST(L1CapabilitiesTests, CreateFactoryAndObject)
{
    CONFORMANCE_CORE_TEST();

    auto factory = IMediaPipelineCapabilitiesFactory::createFactory();
    UT_ASSERT_NOT_NULL_FATAL(factory.get());

    auto caps = factory->createMediaPipelineCapabilities();
    UT_ASSERT_NOT_NULL_FATAL(caps.get());
}

/**
 * getSupportedMimeTypes(VIDEO) reports a non-empty list, and isMimeTypeSupported()
 * agrees with it for H.264.
 *
 * NOT gated: H.264 is a baseline-required codec, so it is tested
 * unconditionally — its absence is a conformance FAILURE, not a skip.
 */
UT_ADD_TEST(L1CapabilitiesTests, BaselineVideoMimeTypeSupportIsConsistent)
{
    CONFORMANCE_CORE_TEST();

    auto factory = IMediaPipelineCapabilitiesFactory::createFactory();
    UT_ASSERT_NOT_NULL_FATAL(factory.get());
    auto caps = factory->createMediaPipelineCapabilities();
    UT_ASSERT_NOT_NULL_FATAL(caps.get());

    const std::vector<std::string> videoMimes = caps->getSupportedMimeTypes(MediaSourceType::VIDEO);
    UT_ASSERT_FALSE(videoMimes.empty());

    // isMimeTypeSupported() must agree with the advertised list, and H.264 is
    // required to be present.
    const std::string h264{"video/h264"};
    const bool inList = std::find(videoMimes.begin(), videoMimes.end(), h264) != videoMimes.end();
    UT_ASSERT_EQUAL(caps->isMimeTypeSupported(h264), inList);
    UT_ASSERT_TRUE(caps->isMimeTypeSupported(h264));
}

/**
 * Where the target's platform declares AV1, the advertised list and
 * isMimeTypeSupported() must both confirm it.
 *
 * GATED on a genuinely-variable feature ("codecs.video.av1"): a target whose
 * platform does not support AV1 self-skips rather than failing (capability gate,
 * §3 req 2). This is the pattern for every optional capability.
 */
UT_ADD_TEST(L1CapabilitiesTests, Av1MimeTypeSupportedWhenDeclared)
{
    CONFORMANCE_CORE_TEST();
    CONFORMANCE_REQUIRE_CAP("codecs.video.av1");

    auto factory = IMediaPipelineCapabilitiesFactory::createFactory();
    UT_ASSERT_NOT_NULL_FATAL(factory.get());
    auto caps = factory->createMediaPipelineCapabilities();
    UT_ASSERT_NOT_NULL_FATAL(caps.get());

    const std::string av1{"video/x-av1"};
    UT_ASSERT_TRUE(caps->isMimeTypeSupported(av1));
}

/**
 * isVideoMaster() returns a status and writes its out-param. Entered at ABI v3
 * (video-master); a backend below v3 self-skips via the ABI gate (§4).
 */
UT_ADD_TEST(L1CapabilitiesTests, IsVideoMasterReportsStatus)
{
    CONFORMANCE_CORE_TEST();
    CONFORMANCE_REQUIRE_ABI(3);

    auto factory = IMediaPipelineCapabilitiesFactory::createFactory();
    UT_ASSERT_NOT_NULL_FATAL(factory.get());
    auto caps = factory->createMediaPipelineCapabilities();
    UT_ASSERT_NOT_NULL_FATAL(caps.get());

    bool isMaster = false;
    UT_ASSERT_TRUE(caps->isVideoMaster(isMaster));
}
