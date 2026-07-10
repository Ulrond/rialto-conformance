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
 * @file CapsTests.cpp
 *
 * L1 — function testing for Surface A: the sink-pad template and the MIME->caps
 * mapping advertised by the rialtomse{audio,video,subtitle}sink elements. An
 * external media app links its pipeline to these sinks by negotiating caps
 * against their sink-pad template, so the suite introspects that template exactly
 * as GStreamer presents it — its direction/presence/name and the caps it carries.
 * No Rialto internal headers are involved (§2 Surface A).
 *
 * The template caps are the intersection of the documented MIME->caps mapping
 * with what the connected server reports as supported, so the conformance
 * assertion is *soundness*: every advertised cap is one the documented mapping
 * defines (a server MIME with no mapping entry is dropped, never advertised).
 * Completeness is platform-variable and is only asserted for the baseline-
 * required surface (a non-empty audio template and the H.264 video caps).
 *
 * Coverage trace: coverage/rc-core-catalog.yaml — RC-CORE-MSECAPS-001..005.
 * (MSECAPS-006, incoming CAPS-event field parsing, is a data-flow transform
 * driven with an attached source and is covered with the MSESTATE/DATA work.)
 */

#include <ut.h>

#include "conformance/Surfaces.h"
#include "conformance/TierGate.h"

#include <gst/gst.h>

#include <cstddef>

using rialto::conformance::MseSinkSurface;

namespace
{
// The documented MIME->caps mapping (rialto-gstreamer GStreamerMSEUtils), as the
// caps universe each sink is allowed to advertise. Every cap a sink advertises
// must fall within its surface's set; nothing outside it may appear.
const char *const kDocumentedAudioCaps[] = {
    "audio/mpeg, mpegversion=1", "audio/mpeg, mpegversion=2", "audio/mpeg, mpegversion=4",
    "audio/x-ac3",               "audio/x-eac3",              "audio/x-opus",
    "audio/b-wav",               "audio/x-flac",              "audio/x-raw"};

const char *const kDocumentedVideoCaps[] = {"video/x-h264", "video/x-h265", "video/x-av1", "video/x-vp9"};

const char *const kDocumentedSubtitleCaps[] = {"text/vtt",
                                               "application/x-subtitle-vtt",
                                               "application/ttml+xml",
                                               "closedcaption/x-cea-608",
                                               "closedcaption/x-cea-708",
                                               "application/x-cea-608",
                                               "application/x-cea-708",
                                               "application/x-subtitle-cc"};

class L1CapsTests : public MseSinkSurface
{
};

// The sink-pad template of an element by name, or FATAL (the sinks always carry
// one — its absence is a conformance failure, not a soft skip).
GstPadTemplate *sinkPadTemplate(GstElement *element)
{
    GstPadTemplate *tmpl = gst_element_class_get_pad_template(GST_ELEMENT_GET_CLASS(element), "sink");
    return tmpl; // caller null-checks; FATAL cannot live in a value-returning helper
}

// The caps carried by the element's sink-pad template (transfer-full; caller
// unrefs), or nullptr if there is no such template.
GstCaps *sinkTemplateCaps(GstElement *element)
{
    GstPadTemplate *tmpl = sinkPadTemplate(element);
    if (tmpl == nullptr)
        return nullptr;
    return gst_pad_template_get_caps(tmpl);
}

// Build one GstCaps that is the union of the documented caps strings.
GstCaps *documentedUnion(const char *const *docCaps, std::size_t n)
{
    GstCaps *caps = gst_caps_new_empty();
    for (std::size_t i = 0; i < n; ++i)
        gst_caps_append(caps, gst_caps_from_string(docCaps[i]));
    return caps;
}

// Assert every cap the sink advertises falls within the documented universe for
// its surface (soundness: no foreign/incorrectly-formed cap is advertised).
void assertAdvertisedWithinDocumented(GstElement *sink, const char *const *docCaps, std::size_t n)
{
    GstCaps *advertised = sinkTemplateCaps(sink);
    UT_ASSERT_NOT_NULL(advertised);
    if (advertised == nullptr)
        return;
    GstCaps *documented = documentedUnion(docCaps, n);
    UT_ASSERT_TRUE(gst_caps_is_subset(advertised, documented));
    gst_caps_unref(documented);
    gst_caps_unref(advertised);
}

// True when the sink advertises (a superset of) the given caps string.
bool advertises(GstElement *sink, const char *capsStr)
{
    GstCaps *advertised = sinkTemplateCaps(sink);
    if (advertised == nullptr)
        return false;
    GstCaps *one = gst_caps_from_string(capsStr);
    const bool result = gst_caps_is_subset(one, advertised);
    gst_caps_unref(one);
    gst_caps_unref(advertised);
    return result;
}

// Assert the element exposes exactly one pad template: an ALWAYS sink pad named
// "sink" (RC-CORE-MSECAPS-001, shared by every sink).
void assertSingleAlwaysSinkTemplate(GstElement *sink)
{
    GList *templates = gst_element_class_get_pad_template_list(GST_ELEMENT_GET_CLASS(sink));
    UT_ASSERT_EQUAL(g_list_length(templates), 1u);
    UT_ASSERT_NOT_NULL_FATAL(templates);

    GstPadTemplate *tmpl = GST_PAD_TEMPLATE(templates->data);
    UT_ASSERT_EQUAL(GST_PAD_TEMPLATE_DIRECTION(tmpl), GST_PAD_SINK);
    UT_ASSERT_EQUAL(GST_PAD_TEMPLATE_PRESENCE(tmpl), GST_PAD_ALWAYS);
    UT_ASSERT_TRUE(g_str_equal(GST_PAD_TEMPLATE_NAME_TEMPLATE(tmpl), "sink"));
}
} // namespace

UT_ADD_TEST_TO_GROUP(L1CapsTests, UT_TESTS_L1);

/**
 * RC-CORE-MSECAPS-001 — each sink (audio, video, subtitle) exposes exactly one
 * ALWAYS sink pad template named "sink".
 */
UT_ADD_TEST(L1CapsTests, SinkPadTemplateIsSingleAlwaysSink)
{
    CONFORMANCE_CORE_TEST();
    for (const char *factory : {kAudioSink, kVideoSink, kSubtitleSink})
    {
        GstElement *sink = gst_element_factory_make(factory, nullptr);
        UT_ASSERT_NOT_NULL_FATAL(sink);
        assertSingleAlwaysSinkTemplate(sink);
        gst_object_unref(sink);
    }
}

/**
 * RC-CORE-MSECAPS-002 — a sink advertises exactly the supported subset of the
 * documented MIME->caps mapping; a server MIME absent from the mapping is dropped
 * rather than advertised. Verified as soundness across all three sinks: every
 * advertised cap lies within that sink's documented universe.
 */
UT_ADD_TEST(L1CapsTests, AdvertisedCapsAreDocumentedSubset)
{
    CONFORMANCE_CORE_TEST();

    GstElement *audio = gst_element_factory_make(kAudioSink, nullptr);
    GstElement *video = gst_element_factory_make(kVideoSink, nullptr);
    GstElement *subtitle = gst_element_factory_make(kSubtitleSink, nullptr);
    UT_ASSERT_NOT_NULL_FATAL(audio);
    UT_ASSERT_NOT_NULL_FATAL(video);
    UT_ASSERT_NOT_NULL_FATAL(subtitle);

    assertAdvertisedWithinDocumented(audio, kDocumentedAudioCaps, G_N_ELEMENTS(kDocumentedAudioCaps));
    assertAdvertisedWithinDocumented(video, kDocumentedVideoCaps, G_N_ELEMENTS(kDocumentedVideoCaps));
    assertAdvertisedWithinDocumented(subtitle, kDocumentedSubtitleCaps, G_N_ELEMENTS(kDocumentedSubtitleCaps));

    gst_object_unref(audio);
    gst_object_unref(video);
    gst_object_unref(subtitle);
}

/**
 * RC-CORE-MSECAPS-003 — the audio MIME->caps mapping is correct: every advertised
 * audio cap is a documented one, and the audio template is non-empty (audio is a
 * baseline-required surface).
 */
UT_ADD_TEST(L1CapsTests, AudioMimeToCapsMappingCorrect)
{
    CONFORMANCE_CORE_TEST();
    GstElement *sink = gst_element_factory_make(kAudioSink, nullptr);
    UT_ASSERT_NOT_NULL_FATAL(sink);

    assertAdvertisedWithinDocumented(sink, kDocumentedAudioCaps, G_N_ELEMENTS(kDocumentedAudioCaps));

    GstCaps *advertised = sinkTemplateCaps(sink);
    UT_ASSERT_NOT_NULL_FATAL(advertised);
    UT_ASSERT_FALSE(gst_caps_is_empty(advertised));
    gst_caps_unref(advertised);

    gst_object_unref(sink);
}

/**
 * RC-CORE-MSECAPS-004 — the video MIME->caps mapping is correct: every advertised
 * video cap is a documented one, and the sink advertises video/x-h264 (H.264 is a
 * baseline-required codec, tested unconditionally).
 */
UT_ADD_TEST(L1CapsTests, VideoMimeToCapsMappingCorrect)
{
    CONFORMANCE_CORE_TEST();
    GstElement *sink = gst_element_factory_make(kVideoSink, nullptr);
    UT_ASSERT_NOT_NULL_FATAL(sink);

    assertAdvertisedWithinDocumented(sink, kDocumentedVideoCaps, G_N_ELEMENTS(kDocumentedVideoCaps));
    UT_ASSERT_TRUE(advertises(sink, "video/x-h264"));

    gst_object_unref(sink);
}

/**
 * RC-CORE-MSECAPS-005 — the subtitle MIME->caps mapping is correct: every
 * advertised subtitle cap is a documented one. Subtitle support is platform-
 * variable, so only soundness is required (the template may be empty where the
 * platform advertises no text tracks).
 */
UT_ADD_TEST(L1CapsTests, SubtitleMimeToCapsMappingCorrect)
{
    CONFORMANCE_CORE_TEST();
    GstElement *sink = gst_element_factory_make(kSubtitleSink, nullptr);
    UT_ASSERT_NOT_NULL_FATAL(sink);

    assertAdvertisedWithinDocumented(sink, kDocumentedSubtitleCaps, G_N_ELEMENTS(kDocumentedSubtitleCaps));

    gst_object_unref(sink);
}
