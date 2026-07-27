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
 * L1 — function testing for mseSink interface: rialtomse*sink element registration,
 * naming, rank-gating, and factory metadata. The app sees these elements exactly
 * as the GStreamer registry exposes them — by factory name, with the klass and
 * interfaces it introspects to plug them into a pipeline — so the suite resolves
 * and inspects them the same way; no Rialto internal headers are involved
 * (§2 mseSink interface).
 *
 * Coverage trace: coverage/rc-core-catalog.yaml — RC-CORE-MSE-001 (availability +
 * naming), RC-CORE-MSE-002 (rank-gated registration), RC-CORE-MSE-003 (factory
 * metadata/klass + the audio sink's GstStreamVolume interface).
 */

#include <ut.h>

#include "conformance/RegistrationProbe.h"
#include "conformance/Surfaces.h"
#include "conformance/TierGate.h"

#include <gst/audio/streamvolume.h>
#include <gst/gst.h>

#include <climits>
#include <string>

#include <sys/wait.h>
#include <unistd.h>

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

// Assert a factory advertises the documented GST_ELEMENT_METADATA_KLASS. The
// klass is the pipeline-classification string an autoplugger keys on, so it is
// part of the element's published contract.
void assertSinkKlass(const char *factoryName, const char *expectedKlass)
{
    GstElementFactory *factory = gst_element_factory_find(factoryName);
    UT_ASSERT_NOT_NULL_FATAL(factory);

    const gchar *klass = gst_element_factory_get_metadata(factory, GST_ELEMENT_METADATA_KLASS);
    UT_ASSERT_NOT_NULL(klass);
    if (klass != nullptr)
        UT_ASSERT_STRING_EQUAL(klass, expectedKlass);

    gst_object_unref(factory);
}

// Run the registration probe (this same binary, re-execed) in a fresh process
// whose GStreamer registry is scanned from scratch, and return its verdict for
// @p factory: true = registered, false = absent. @p rankEnv is the shell prelude
// that sets the rank inputs the rialtosinks plugin_init reads (RIALTO_SOCKET_PATH
// / RIALTO_SINKS_RANK); a private GST_REGISTRY forces plugin_init to re-run under
// exactly that env rather than loading the gate process's cached result.
bool probeFactoryRegistered(const char *factory, const char *rankEnv)
{
    char self[PATH_MAX];
    const ssize_t len = readlink("/proc/self/exe", self, sizeof(self) - 1);
    UT_ASSERT_TRUE(len > 0);
    if (len <= 0)
        return false;
    self[len] = '\0';

    // GST_REGISTRY_FORK=no keeps the fresh scan in-process; the temp registry is
    // removed afterwards so nothing leaks between probes.
    const std::string script = std::string(rankEnv) +
                               " reg=\"$(mktemp)\";"
                               " GST_REGISTRY=\"$reg\" GST_REGISTRY_FORK=no"
                               " RIALTO_CONFORMANCE_PROBE_FACTORY=\"" +
                               factory + "\" \"" + self +
                               "\";"
                               " rc=$?; rm -f \"$reg\"; exit $rc";

    gchar *argv[] = {const_cast<gchar *>("/bin/sh"), const_cast<gchar *>("-c"),
                     const_cast<gchar *>(script.c_str()), nullptr};
    gint status = 0;
    GError *error = nullptr;
    const gboolean spawned =
        g_spawn_sync(nullptr, argv, nullptr, G_SPAWN_DEFAULT, nullptr, nullptr, nullptr, nullptr, &status, &error);
    UT_ASSERT_TRUE(spawned);
    if (!spawned)
    {
        if (error != nullptr)
            g_error_free(error);
        return false;
    }
    UT_ASSERT_TRUE(WIFEXITED(status));
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

// Rank inputs for the two probe legs: rank 0 (neither input set) vs. an explicit
// rank override. RIALTO_SOCKET_PATH is cleared in both so the rank is driven only
// by RIALTO_SINKS_RANK — isolating the documented gating variable.
constexpr const char *kRankZeroEnv = "unset RIALTO_SOCKET_PATH RIALTO_SINKS_RANK;";
constexpr const char *kRankOneEnv = "unset RIALTO_SOCKET_PATH; export RIALTO_SINKS_RANK=1;";
} // namespace

UT_ADD_TEST_TO_GROUP(L1SinkRegistrationTests, UT_TESTS_L1);

// The MSE sinks are the required mseSink interface — tested unconditionally. Their
// absence is a conformance FAILURE, not a capability skip, so these cases are
// not gated.

/**
 * rialtomsevideosink is registered under its documented name and instantiates.
 */
UT_ADD_TEST(L1SinkRegistrationTests, VideoSinkRegistered)
{
    CONFORMANCE_CORE_TEST();
    assertSinkRegistered(kVideoSink);
}

/**
 * rialtomseaudiosink is registered under its documented name and instantiates.
 */
UT_ADD_TEST(L1SinkRegistrationTests, AudioSinkRegistered)
{
    CONFORMANCE_CORE_TEST();
    assertSinkRegistered(kAudioSink);
}

/**
 * RC-CORE-MSE-002 — registration is rank-gated: when the computed rank is 0
 * (RIALTO_SOCKET_PATH unset and RIALTO_SINKS_RANK not > 0) the plugin registers
 * no elements at all; a rank > 0 registers all sinks. Observed in a fresh child
 * process because the gate process already loaded the sinks with a non-zero rank.
 * The rank > 0 leg is the positive control — it proves the probe genuinely
 * detects registration (guarding against a probe that reports "absent"
 * unconditionally), so the rank-0 absence is meaningful.
 */
UT_ADD_TEST(L1SinkRegistrationTests, RegistrationIsRankGated)
{
    CONFORMANCE_CORE_TEST();

    // Rank 0: no sink registers.
    UT_ASSERT_FALSE(probeFactoryRegistered(kVideoSink, kRankZeroEnv));
    UT_ASSERT_FALSE(probeFactoryRegistered(kAudioSink, kRankZeroEnv));
    UT_ASSERT_FALSE(probeFactoryRegistered(kSubtitleSink, kRankZeroEnv));

    // Rank > 0: the same sinks register (positive control).
    UT_ASSERT_TRUE(probeFactoryRegistered(kVideoSink, kRankOneEnv));
    UT_ASSERT_TRUE(probeFactoryRegistered(kAudioSink, kRankOneEnv));
    UT_ASSERT_TRUE(probeFactoryRegistered(kSubtitleSink, kRankOneEnv));
}

/**
 * RC-CORE-MSE-003 — each sink advertises its documented factory metadata klass:
 * video=Decoder/Video/Sink/Video, audio=Decoder/Audio/Sink/Audio,
 * subtitle=Parser/Subtitle/Sink/Subtitle.
 */
UT_ADD_TEST(L1SinkRegistrationTests, SinkFactoryKlassMetadata)
{
    CONFORMANCE_CORE_TEST();
    assertSinkKlass(kVideoSink, "Decoder/Video/Sink/Video");
    assertSinkKlass(kAudioSink, "Decoder/Audio/Sink/Audio");
    assertSinkKlass(kSubtitleSink, "Parser/Subtitle/Sink/Subtitle");
}

/**
 * RC-CORE-MSE-003 — the audio sink implements the GstStreamVolume interface, so
 * an app can drive linear/dB volume through the standard GStreamer interface
 * rather than the sink's own property. The type advertises the interface, and a
 * live instance is usable as a GstStreamVolume.
 */
UT_ADD_TEST(L1SinkRegistrationTests, AudioSinkImplementsStreamVolume)
{
    CONFORMANCE_CORE_TEST();
    GstElement *audio = gst_element_factory_make(kAudioSink, nullptr);
    UT_ASSERT_NOT_NULL_FATAL(audio);

    UT_ASSERT_TRUE(g_type_is_a(G_OBJECT_TYPE(audio), GST_TYPE_STREAM_VOLUME));
    UT_ASSERT_TRUE(GST_IS_STREAM_VOLUME(audio));

    gst_object_unref(audio);
}
