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
 * @file SinkRegistrationTests.cpp
 *
 * L1 — function testing for Surface A: rialtomse*sink element registration and
 * naming. The app sees these elements exactly as the GStreamer registry exposes
 * them — by factory name — so the suite resolves them the same way; no Rialto
 * internal headers are involved (§2 Surface A).
 *
 * Coverage trace: coverage/matrix.yaml row prop:element-registration
 * (RC-MSE-001 — MSE sink element availability + naming).
 */

#include <ut.h>

#include "conformance/Surfaces.h"

#include <gst/gst.h>

using rialto::conformance::MseSinkSurface;

namespace
{
class L1SinkRegistrationTests : public MseSinkSurface
{
};

// Confirm a named sink factory is present in the registry and instantiates.
void assertSinkRegistered(const char *factoryName)
{
    GstElementFactory *factory = gst_element_factory_find(factoryName);
    UT_ASSERT_NOT_NULL_FATAL(factory);

    GstElement *element = gst_element_factory_create(factory, nullptr);
    UT_ASSERT_NOT_NULL(element);
    if (element != nullptr)
    {
        gst_object_unref(element);
    }
    gst_object_unref(factory);
}
} // namespace

UT_ADD_TEST_TO_GROUP(L1SinkRegistrationTests, UT_TESTS_L1);

// The MSE sinks are the required Surface A — tested unconditionally. Their
// absence is a conformance FAILURE, not a capability skip, so these cases are
// not gated.

/**
 * rialtomsevideosink is registered under its documented name and instantiates.
 */
UT_ADD_TEST(L1SinkRegistrationTests, VideoSinkRegistered)
{
    assertSinkRegistered(kVideoSink);
}

/**
 * rialtomseaudiosink is registered under its documented name and instantiates.
 */
UT_ADD_TEST(L1SinkRegistrationTests, AudioSinkRegistered)
{
    assertSinkRegistered(kAudioSink);
}
