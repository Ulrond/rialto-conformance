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
 * isVideoMaster() returns a status and writes its out-param. Part of the
 * capabilities surface in the targeted Rialto release, so it applies
 * unconditionally; a CONFORMANCE_REQUIRE_SINCE gate would be added only if a
 * future requirement needed a newer Rialto release.
 */
UT_ADD_TEST(L1CapabilitiesTests, IsVideoMasterReportsStatus)
{
    CONFORMANCE_CORE_TEST();

    auto factory = IMediaPipelineCapabilitiesFactory::createFactory();
    UT_ASSERT_NOT_NULL_FATAL(factory.get());
    auto caps = factory->createMediaPipelineCapabilities();
    UT_ASSERT_NOT_NULL_FATAL(caps.get());

    bool isMaster = false;
    UT_ASSERT_TRUE(caps->isVideoMaster(isMaster));
}

/**
 * RC-CORE-CAPS-004 — getSupportedMimeTypes reports a per-source-type list for
 * AUDIO, VIDEO and SUBTITLE. VIDEO and AUDIO must be non-empty (baseline H.264 +
 * AAC are mandatory); SUBTITLE content varies, so it is only required to return.
 */
UT_ADD_TEST(L1CapabilitiesTests, SupportedMimeTypesPerSourceType)
{
    CONFORMANCE_CORE_TEST();

    auto factory = IMediaPipelineCapabilitiesFactory::createFactory();
    UT_ASSERT_NOT_NULL_FATAL(factory.get());
    auto caps = factory->createMediaPipelineCapabilities();
    UT_ASSERT_NOT_NULL_FATAL(caps.get());

    UT_ASSERT_FALSE(caps->getSupportedMimeTypes(MediaSourceType::VIDEO).empty());
    UT_ASSERT_FALSE(caps->getSupportedMimeTypes(MediaSourceType::AUDIO).empty());

    // SUBTITLE support is platform-variable; the call must be valid regardless.
    const std::vector<std::string> subtitle = caps->getSupportedMimeTypes(MediaSourceType::SUBTITLE);
    (void)subtitle;
}

/**
 * RC-CORE-CAPS-003 — getSupportedProperties returns the subset of the queried
 * names that are supported (never a name that was not queried), an unknown name
 * is never reported, and MediaSourceType::UNKNOWN searches AUDIO and VIDEO (so
 * its result is the union of the per-type results).
 */
UT_ADD_TEST(L1CapabilitiesTests, SupportedPropertiesContract)
{
    CONFORMANCE_CORE_TEST();

    auto factory = IMediaPipelineCapabilitiesFactory::createFactory();
    UT_ASSERT_NOT_NULL_FATAL(factory.get());
    auto caps = factory->createMediaPipelineCapabilities();
    UT_ASSERT_NOT_NULL_FATAL(caps.get());

    // A mix of documented server-conditional sink properties plus one that can
    // never exist. Whichever are supported is platform-variable; the contract is
    // the shape of the result, not which names are present.
    const std::string bogus{"rc-core-nonexistent-property"};
    const std::vector<std::string> queried{"low-latency", "sync", "audio-fade", "immediate-output", bogus};

    auto isSubsetOfQuery = [&](const std::vector<std::string> &result)
    {
        for (const std::string &name : result)
        {
            if (std::find(queried.begin(), queried.end(), name) == queried.end())
            {
                return false;
            }
        }
        return true;
    };
    auto contains = [](const std::vector<std::string> &v, const std::string &s)
    { return std::find(v.begin(), v.end(), s) != v.end(); };

    const std::vector<std::string> audio = caps->getSupportedProperties(MediaSourceType::AUDIO, queried);
    const std::vector<std::string> video = caps->getSupportedProperties(MediaSourceType::VIDEO, queried);
    const std::vector<std::string> unknown = caps->getSupportedProperties(MediaSourceType::UNKNOWN, queried);

    // Every returned name was queried; the impossible name is never returned.
    UT_ASSERT_TRUE(isSubsetOfQuery(audio));
    UT_ASSERT_TRUE(isSubsetOfQuery(video));
    UT_ASSERT_TRUE(isSubsetOfQuery(unknown));
    UT_ASSERT_FALSE(contains(unknown, bogus));

    // UNKNOWN searches AUDIO and VIDEO, so it reports anything either reports.
    for (const std::string &name : audio)
    {
        UT_ASSERT_TRUE(contains(unknown, name));
    }
    for (const std::string &name : video)
    {
        UT_ASSERT_TRUE(contains(unknown, name));
    }
}
