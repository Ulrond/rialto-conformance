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
 * @file CapabilityConsistencyTests.cpp
 *
 * L2 — cross-surface consistency (path: both). A requirement exposed on both
 * surfaces must be answered the same way by each: Firebolt interface (the native client
 * API) and mseSink interface (the rialtomse*sink elements) sit on one backend, so a
 * backend fact reported differently by the two is a conformance failure even
 * when each surface is individually well-formed. Each surface runs as itself —
 * the native API is queried through its factories, the sinks through the
 * GStreamer registry — and the case asserts the answers agree.
 *
 * Note on scope: the two surfaces cannot share a single backend session (each
 * client owns its own), so consistency is asserted for session-independent
 * facts (capability answers, template caps, property defaults/ranges) — not
 * per-session state.
 *
 * Coverage trace: coverage/rc-core-catalog.yaml / matrix.yaml rows
 * RC-CORE-CONSIST-001 (native supported MIME types map into the matching sink's
 * advertised caps), RC-CORE-CONSIST-002 (isVideoMaster agrees with the video
 * sink's is-master property), RC-CORE-CONSIST-003 (the volume contract agrees
 * across surfaces), RC-CORE-CONSIST-004 (the mute contract agrees across
 * surfaces), RC-CORE-CONSIST-005 (the optional-property set agrees across
 * surfaces). The classification behind these rows is coverage/
 * cross-surface-fact-inventory.md.
 */

#include <ut.h>
#include <ut_log.h>

#include "conformance/Surfaces.h"
#include "conformance/TierGate.h"

#include "IMediaPipelineCapabilities.h"
#include "MediaCommon.h"

#include <gst/gst.h>

#include <cstddef>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace firebolt::rialto;
using rialto::conformance::MseSinkSurface;

namespace
{
/**
 * The documented MIME -> caps mapping the sinks build their pad templates from
 * (the published mseSink interface translation of the server's Firebolt interface MIME list).
 * A native MIME with no entry here is not advertised on mseSink interface by design.
 */
const std::map<std::string, std::vector<std::string>> kMimeToCaps = {
    {"audio/mp4", {"audio/mpeg, mpegversion=1", "audio/mpeg, mpegversion=2", "audio/mpeg, mpegversion=4"}},
    {"audio/mp3", {"audio/mpeg, mpegversion=1", "audio/mpeg, mpegversion=2"}},
    {"audio/aac", {"audio/mpeg, mpegversion=2", "audio/mpeg, mpegversion=4"}},
    {"audio/x-eac3", {"audio/x-ac3", "audio/x-eac3"}},
    {"audio/x-opus", {"audio/x-opus"}},
    {"audio/b-wav", {"audio/b-wav"}},
    {"audio/x-flac", {"audio/x-flac"}},
    {"audio/x-raw", {"audio/x-raw"}},
    {"video/h264", {"video/x-h264"}},
    {"video/h265", {"video/x-h265"}},
    {"video/x-av1", {"video/x-av1"}},
    {"video/x-vp9", {"video/x-vp9"}},
};

/// The caps a sink's single "sink" pad template advertises (caller unrefs).
GstCaps *sinkTemplateCaps(const char *factoryName)
{
    GstElement *sink = gst_element_factory_make(factoryName, nullptr);
    if (sink == nullptr)
        return nullptr;
    GstCaps *caps = nullptr;
    GstPadTemplate *tmpl = gst_element_class_get_pad_template(GST_ELEMENT_GET_CLASS(sink), "sink");
    if (tmpl != nullptr)
        caps = gst_pad_template_get_caps(tmpl);
    gst_object_unref(sink);
    return caps;
}

/// True when @p advertised contains (a superset of) the caps string @p capsStr.
bool advertises(GstCaps *advertised, const std::string &capsStr)
{
    GstCaps *one = gst_caps_from_string(capsStr.c_str());
    const bool result = gst_caps_is_subset(one, advertised);
    gst_caps_unref(one);
    return result;
}

class L2CapabilityConsistencyTests : public MseSinkSurface
{
};
} // namespace

UT_ADD_TEST_TO_GROUP(L2CapabilityConsistencyTests, UT_TESTS_L2);

/**
 * RC-CORE-CONSIST-001 — every codec the native capabilities API reports
 * (getSupportedMimeTypes for AUDIO and VIDEO) that has a documented mseSink interface
 * translation is advertised by the matching sink's pad template. The two
 * surfaces answer the same backend fact — "which formats does this platform
 * play?" — and must agree.
 */
UT_ADD_TEST(L2CapabilityConsistencyTests, SupportedMimesMatchSinkCaps)
{
    CONFORMANCE_CORE_TEST();

    auto factory = IMediaPipelineCapabilitiesFactory::createFactory();
    UT_ASSERT_NOT_NULL_FATAL(factory.get());
    std::unique_ptr<IMediaPipelineCapabilities> caps = factory->createMediaPipelineCapabilities();
    UT_ASSERT_NOT_NULL_FATAL(caps.get());

    const struct
    {
        MediaSourceType type;
        const char *sinkFactory;
    } kSurfacePairs[] = {{MediaSourceType::AUDIO, kAudioSink}, {MediaSourceType::VIDEO, kVideoSink}};

    for (const auto &pair : kSurfacePairs)
    {
        const std::vector<std::string> mimes = caps->getSupportedMimeTypes(pair.type);
        GstCaps *advertised = sinkTemplateCaps(pair.sinkFactory);
        UT_ASSERT_NOT_NULL_FATAL(advertised);

        std::size_t checked = 0;
        for (const std::string &mime : mimes)
        {
            const auto mapping = kMimeToCaps.find(mime);
            if (mapping == kMimeToCaps.end())
                continue; // no documented mseSink interface translation for this MIME
            for (const std::string &capsStr : mapping->second)
            {
                UT_ASSERT_TRUE(advertises(advertised, capsStr));
                ++checked;
            }
        }
        UT_LOG("[consistency] %s: %zu native MIMEs, %zu mapped caps all advertised",
               pair.sinkFactory, mimes.size(), checked);
        // The native list must have yielded at least one mapped cap per surface
        // (baseline H.264 / AAC are required), or the comparison proved nothing.
        UT_ASSERT_TRUE(checked > 0);
        gst_caps_unref(advertised);
    }
}

/**
 * RC-CORE-CONSIST-002 — native isVideoMaster() and the rialtomsevideosink
 * `is-master` property answer the same backend fact and must agree.
 */
UT_ADD_TEST(L2CapabilityConsistencyTests, VideoMasterNativeMatchesMse)
{
    CONFORMANCE_CORE_TEST();

    // Firebolt interface: the native capabilities answer.
    auto factory = IMediaPipelineCapabilitiesFactory::createFactory();
    UT_ASSERT_NOT_NULL_FATAL(factory.get());
    std::unique_ptr<IMediaPipelineCapabilities> caps = factory->createMediaPipelineCapabilities();
    UT_ASSERT_NOT_NULL_FATAL(caps.get());
    bool nativeIsMaster = false;
    UT_ASSERT_TRUE_FATAL(caps->isVideoMaster(nativeIsMaster));

    // mseSink interface: the video sink's read-only is-master property.
    GstElement *sink = gst_element_factory_make(kVideoSink, nullptr);
    UT_ASSERT_NOT_NULL_FATAL(sink);
    gboolean sinkIsMaster = FALSE;
    g_object_get(sink, "is-master", &sinkIsMaster, nullptr);
    gst_object_unref(sink);

    UT_LOG("[consistency] isVideoMaster native=%d mse=%d", nativeIsMaster, sinkIsMaster != FALSE);
    UT_ASSERT_EQUAL(nativeIsMaster, sinkIsMaster != FALSE);
}

/**
 * RC-CORE-CONSIST-003 — the volume contract agrees across surfaces: both accept
 * the [0.0, 1.0] range and report the same default (1.0). The surfaces cannot
 * share one backend session, so the session-independent contract — range +
 * default — is the consistency that is assertable; the per-surface round-trips
 * are covered by their own rows (PIPE-017/018 native, MSEPROP-003 mse).
 */
UT_ADD_TEST(L2CapabilityConsistencyTests, VolumeContractAgreesAcrossSurfaces)
{
    CONFORMANCE_CORE_TEST();

    // mseSink interface: the audio sink's volume property — default and range from the
    // installed GParamSpec, the published contract of the property.
    GstElement *sink = gst_element_factory_make(kAudioSink, nullptr);
    UT_ASSERT_NOT_NULL_FATAL(sink);
    GParamSpec *spec = g_object_class_find_property(G_OBJECT_GET_CLASS(sink), "volume");
    UT_ASSERT_NOT_NULL_FATAL(spec);
    GParamSpecDouble *dspec = G_PARAM_SPEC_DOUBLE(spec);
    UT_LOG("[consistency] mse volume: default=%.2f range=[%.2f, %.2f]", dspec->default_value, dspec->minimum,
           dspec->maximum);
    UT_ASSERT_TRUE(dspec->default_value == 1.0);
    UT_ASSERT_TRUE(dspec->minimum == 0.0);
    UT_ASSERT_TRUE(dspec->maximum == 1.0);

    // The standalone sink reads back its documented default.
    gdouble sinkVolume = -1.0;
    g_object_get(sink, "volume", &sinkVolume, nullptr);
    gst_object_unref(sink);
    UT_ASSERT_TRUE(sinkVolume == 1.0);

    // Firebolt interface carries the same contract: setVolume documents targetVolume in
    // [0.0, 1.0] and the realized-pipeline default reads 1.0 — asserted by the
    // native data-path row (L4PipelineAuxDataPathTests.VolumeRoundTrips...); here
    // the agreement is that mseSink interface publishes the identical range + default.
}

/**
 * RC-CORE-CONSIST-004 — the mute contract agrees across surfaces. Mute is a
 * both-surfaces backend fact (native getMute/setMute per source; the audio and
 * subtitle sinks' `mute` property), so the two must carry the same contract.
 * As with volume, the surfaces cannot share one backend session, so the
 * session-independent contract — boolean type + FALSE default — is the
 * assertable consistency; the native per-source round-trip is covered by
 * L4PipelineAuxDataPathTests.MuteRoundTripsPerSource (PIPE-019).
 */
UT_ADD_TEST(L2CapabilityConsistencyTests, MuteContractAgreesAcrossSurfaces)
{
    CONFORMANCE_CORE_TEST();

    // mseSink interface: mute is exposed on both the audio and subtitle sinks; both must
    // publish the identical boolean contract with default FALSE.
    for (const char *factoryName : {kAudioSink, kSubtitleSink})
    {
        GstElement *sink = gst_element_factory_make(factoryName, nullptr);
        UT_ASSERT_NOT_NULL_FATAL(sink);
        GParamSpec *spec = g_object_class_find_property(G_OBJECT_GET_CLASS(sink), "mute");
        UT_ASSERT_NOT_NULL_FATAL(spec);
        UT_ASSERT_TRUE_FATAL(G_IS_PARAM_SPEC_BOOLEAN(spec));
        GParamSpecBoolean *bspec = G_PARAM_SPEC_BOOLEAN(spec);
        UT_LOG("[consistency] %s mute: default=%d", factoryName, bspec->default_value != FALSE);
        UT_ASSERT_TRUE(bspec->default_value == FALSE);

        gboolean sinkMute = TRUE;
        g_object_get(sink, "mute", &sinkMute, nullptr);
        gst_object_unref(sink);
        UT_ASSERT_TRUE(sinkMute == FALSE);
    }

    // Firebolt interface carries the same contract: setMute/getMute round-trips the mute
    // flag per source and the realized-pipeline default reads false — asserted by
    // the native data-path row (L4PipelineAuxDataPathTests.MuteRoundTripsPerSource);
    // here the agreement is that mseSink interface publishes the identical default.
}

/**
 * RC-CORE-CONSIST-005 — a sink's optional-property set is a subset of what native
 * reports supported. Each rialtomse*sink installs a vendor-gated property only if
 * its class-init getSupportedProperties(type, names) query returned that name, so
 * no sink may expose an optional knob the platform disowns: every installed gated
 * property must be reported supported by a native capabilities query.
 *
 * The relationship is subset, not equality: native getSupportedProperties is a
 * live registry scan across all platform elements of the media type, so it also
 * reports properties owned only by other elements (e.g. `sync`, carried by core
 * GstBaseSink audio sinks) that the GST_TYPE_ELEMENT-derived rialto sink does not
 * expose, and its answer grows as plugins register over the process lifetime
 * (see IDG-008). The per-surface conformance of the installed properties is
 * covered by MSEPROP-005 (audio) / MSEPROP-008 (video).
 */
UT_ADD_TEST(L2CapabilityConsistencyTests, InstalledOptionalPropertiesAreNativeSupported)
{
    CONFORMANCE_CORE_TEST();

    auto factory = IMediaPipelineCapabilitiesFactory::createFactory();
    UT_ASSERT_NOT_NULL_FATAL(factory.get());
    std::unique_ptr<IMediaPipelineCapabilities> caps = factory->createMediaPipelineCapabilities();
    UT_ASSERT_NOT_NULL_FATAL(caps.get());

    // The vendor-gated property names each sink queries at class-init, per
    // MediaSourceType. Kept in lock-step with the sink sources
    // (framework/rialto-gstreamer RialtoGStreamerMSE{Audio,Video}Sink.cpp).
    const struct
    {
        MediaSourceType type;
        const char *sinkFactory;
        std::vector<std::string> gatedNames;
    } kSurfaces[] = {
        {MediaSourceType::AUDIO,
         kAudioSink,
         {"low-latency", "sync", "sync-off", "stream-sync-mode", "limit-buffering-ms", "audio-fade", "fade-volume"}},
        {MediaSourceType::VIDEO, kVideoSink, {"immediate-output", "syncmode-streaming", "show-video-window"}},
    };

    for (const auto &surface : kSurfaces)
    {
        const std::vector<std::string> supported = caps->getSupportedProperties(surface.type, surface.gatedNames);
        const std::set<std::string> supportedSet(supported.begin(), supported.end());

        GstElement *sink = gst_element_factory_make(surface.sinkFactory, nullptr);
        UT_ASSERT_NOT_NULL_FATAL(sink);
        GObjectClass *klass = G_OBJECT_GET_CLASS(sink);

        for (const std::string &name : surface.gatedNames)
        {
            const bool nativeSupports = supportedSet.count(name) != 0;
            const bool sinkInstalls = g_object_class_find_property(klass, name.c_str()) != nullptr;
            UT_LOG("[consistency] %s %s: native=%d sink=%d", surface.sinkFactory, name.c_str(), nativeSupports,
                   sinkInstalls);
            // Subset: a sink must not expose a gated property native denies. The
            // converse (native-only, e.g. `sync`) is expected — see IDG-008.
            if (sinkInstalls)
            {
                UT_ASSERT_TRUE(nativeSupports);
            }
        }
        gst_object_unref(sink);
    }
}
