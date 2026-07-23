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
 * @file PropertyTests.cpp
 *
 * L1 — function testing for mseSink interface: the GObject property surface of the
 * rialtomse{audio,video,subtitle}sink elements. An external media app configures
 * these sinks purely through their published GObject properties and GSignals, so
 * the suite introspects the same surface the app sees — the installed GParamSpecs
 * (name, type, flags, default, range) and the class GSignals — via the standard
 * GObject registry. No Rialto internal headers are involved (§2 mseSink interface), and
 * no live RialtoServer is needed: the property surface is a property of the
 * registered element classes themselves.
 *
 * A subset of the audio/video properties are *server-conditional* — installed at
 * class-init only when the connected server reports them as supported. Those are
 * asserted "if installed, conforms to the documented spec"; their absence is not
 * a failure (the case that a server MUST advertise them lives in the capability
 * gate, not here).
 *
 * Coverage trace: coverage/rc-core-catalog.yaml — RC-CORE-MSEPROP-001..010.
 */

#include <ut.h>

#include "conformance/Surfaces.h"
#include "conformance/TierGate.h"

#include <gst/gst.h>

using rialto::conformance::MseSinkSurface;

namespace
{
class L1PropertyTests : public MseSinkSurface
{
};

// Locate an installed property on the element's GObject class. A missing
// unconditional property is a conformance FAILURE (UT_ASSERT_NOT_NULL records
// it); the helpers below early-return on null to avoid dereferencing it. The
// FATAL variant cannot be used here — it embeds a `return;` and so is only valid
// in a void test body, not in a value-returning helper.
GParamSpec *findProp(GstElement *element, const char *name)
{
    GParamSpec *spec = g_object_class_find_property(G_OBJECT_GET_CLASS(element), name);
    UT_ASSERT_NOT_NULL(spec);
    return spec;
}

// Assert a property's read/write flag bits match the documented access.
void assertAccess(GParamSpec *spec, bool readable, bool writable)
{
    UT_ASSERT_EQUAL(static_cast<bool>(spec->flags & G_PARAM_READABLE), readable);
    UT_ASSERT_EQUAL(static_cast<bool>(spec->flags & G_PARAM_WRITABLE), writable);
}

void assertBool(GstElement *e, const char *name, gboolean def, bool readable, bool writable)
{
    GParamSpec *spec = findProp(e, name);
    if (spec == nullptr)
        return;
    UT_ASSERT_EQUAL(G_PARAM_SPEC_VALUE_TYPE(spec), G_TYPE_BOOLEAN);
    assertAccess(spec, readable, writable);
    UT_ASSERT_EQUAL(g_value_get_boolean(g_param_spec_get_default_value(spec)), def);
}

void assertInt(GstElement *e, const char *name, gint min, gint max, gint def, bool readable, bool writable)
{
    GParamSpec *spec = findProp(e, name);
    if (spec == nullptr)
        return;
    UT_ASSERT_EQUAL(G_PARAM_SPEC_VALUE_TYPE(spec), G_TYPE_INT);
    assertAccess(spec, readable, writable);
    UT_ASSERT_EQUAL(G_PARAM_SPEC_INT(spec)->minimum, min);
    UT_ASSERT_EQUAL(G_PARAM_SPEC_INT(spec)->maximum, max);
    UT_ASSERT_EQUAL(g_value_get_int(g_param_spec_get_default_value(spec)), def);
}

void assertInt64(GstElement *e, const char *name, gint64 min, gint64 max, gint64 def, bool readable, bool writable)
{
    GParamSpec *spec = findProp(e, name);
    if (spec == nullptr)
        return;
    UT_ASSERT_EQUAL(G_PARAM_SPEC_VALUE_TYPE(spec), G_TYPE_INT64);
    assertAccess(spec, readable, writable);
    UT_ASSERT_EQUAL(G_PARAM_SPEC_INT64(spec)->minimum, min);
    UT_ASSERT_EQUAL(G_PARAM_SPEC_INT64(spec)->maximum, max);
    UT_ASSERT_EQUAL(g_value_get_int64(g_param_spec_get_default_value(spec)), def);
}

void assertUint(GstElement *e, const char *name, guint min, guint max, guint def, bool readable, bool writable)
{
    GParamSpec *spec = findProp(e, name);
    if (spec == nullptr)
        return;
    UT_ASSERT_EQUAL(G_PARAM_SPEC_VALUE_TYPE(spec), G_TYPE_UINT);
    assertAccess(spec, readable, writable);
    UT_ASSERT_EQUAL(G_PARAM_SPEC_UINT(spec)->minimum, min);
    UT_ASSERT_EQUAL(G_PARAM_SPEC_UINT(spec)->maximum, max);
    UT_ASSERT_EQUAL(g_value_get_uint(g_param_spec_get_default_value(spec)), def);
}

void assertDouble(GstElement *e, const char *name, gdouble min, gdouble max, gdouble def, bool readable, bool writable)
{
    GParamSpec *spec = findProp(e, name);
    if (spec == nullptr)
        return;
    UT_ASSERT_EQUAL(G_PARAM_SPEC_VALUE_TYPE(spec), G_TYPE_DOUBLE);
    assertAccess(spec, readable, writable);
    UT_ASSERT_EQUAL(G_PARAM_SPEC_DOUBLE(spec)->minimum, min);
    UT_ASSERT_EQUAL(G_PARAM_SPEC_DOUBLE(spec)->maximum, max);
    UT_ASSERT_EQUAL(g_value_get_double(g_param_spec_get_default_value(spec)), def);
}

void assertString(GstElement *e, const char *name, bool readable, bool writable)
{
    GParamSpec *spec = findProp(e, name);
    if (spec == nullptr)
        return;
    UT_ASSERT_EQUAL(G_PARAM_SPEC_VALUE_TYPE(spec), G_TYPE_STRING);
    assertAccess(spec, readable, writable);
}

// A pointer- or boxed-typed property (stats, last-sample, gap): assert the exact
// value type and access flags. @p valueType is G_TYPE_POINTER or a boxed GType.
void assertTyped(GstElement *e, const char *name, GType valueType, bool readable, bool writable)
{
    GParamSpec *spec = findProp(e, name);
    if (spec == nullptr)
        return;
    UT_ASSERT_EQUAL(G_PARAM_SPEC_VALUE_TYPE(spec), valueType);
    assertAccess(spec, readable, writable);
}

// True if the class installed @p name; server-conditional props use this so an
// absent property is reported (logged) rather than failed.
bool hasProp(GstElement *e, const char *name)
{
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(e), name) != nullptr)
        return true;
    UT_LOG("[mseprop] server-conditional '%s' not installed by this server; skipping its checks", name);
    return false;
}

// Server-conditional boolean: only asserted when the class installed it.
void assertBoolIfPresent(GstElement *e, const char *name, gboolean def, bool readable, bool writable)
{
    if (hasProp(e, name))
        assertBool(e, name, def, readable, writable);
}
} // namespace

UT_ADD_TEST_TO_GROUP(L1PropertyTests, UT_TESTS_L1);

// The MSE sinks are the required mseSink interface — their documented unconditional
// property surface is tested unconditionally; absence is a FAILURE, not a skip.
// Server-conditional properties are gated in-body ("if installed, conforms").

/**
 * RC-CORE-MSEPROP-001 — base-sink properties exist with the documented
 * types/defaults/flags/ranges. The base surface is shared by every sink; the
 * audio sink is a representative concrete instance of RialtoMSEBaseSink.
 */
UT_ADD_TEST(L1PropertyTests, BaseSinkProperties)
{
    CONFORMANCE_CORE_TEST();
    GstElement *sink = gst_element_factory_make(kAudioSink, nullptr);
    UT_ASSERT_NOT_NULL_FATAL(sink);

    assertBool(sink, "single-path-stream", FALSE, /*R*/ true, /*W*/ true);
    assertInt(sink, "streams-number", 1, G_MAXINT, 1, true, true);
    assertBool(sink, "has-drm", TRUE, true, true);
    assertTyped(sink, "stats", G_TYPE_POINTER, /*R*/ true, /*W*/ false);
    assertBool(sink, "enable-last-sample", FALSE, true, true);
    assertTyped(sink, "last-sample", GST_TYPE_SAMPLE, /*R*/ true, /*W*/ false);

    gst_object_unref(sink);
}

/**
 * RC-CORE-MSEPROP-002 — the base sink exposes the buffer-underflow-callback and
 * first-video-frame-callback GSignals on its class.
 */
UT_ADD_TEST(L1PropertyTests, BaseSinkSignals)
{
    CONFORMANCE_CORE_TEST();
    GstElement *sink = gst_element_factory_make(kAudioSink, nullptr);
    UT_ASSERT_NOT_NULL_FATAL(sink);

    UT_ASSERT_TRUE(g_signal_lookup("buffer-underflow-callback", G_OBJECT_TYPE(sink)) != 0);
    UT_ASSERT_TRUE(g_signal_lookup("first-video-frame-callback", G_OBJECT_TYPE(sink)) != 0);

    gst_object_unref(sink);
}

/**
 * RC-CORE-MSEPROP-003 — audio-sink unconditional properties exist with the
 * documented types/defaults/flags.
 */
UT_ADD_TEST(L1PropertyTests, AudioSinkProperties)
{
    CONFORMANCE_CORE_TEST();
    GstElement *sink = gst_element_factory_make(kAudioSink, nullptr);
    UT_ASSERT_NOT_NULL_FATAL(sink);

    assertDouble(sink, "volume", 0.0, 1.0, 1.0, /*R*/ true, /*W*/ true);
    assertBool(sink, "mute", FALSE, true, true);
    assertTyped(sink, "gap", GST_TYPE_STRUCTURE, /*R*/ false, /*W*/ true);
    assertBool(sink, "use-buffering", FALSE, true, true);
    assertBool(sink, "async", FALSE, true, true);
    assertBool(sink, "web-audio", FALSE, true, true);

    gst_object_unref(sink);
}

/**
 * RC-CORE-MSEPROP-004 — web-audio is settable while the sink is in NULL state; a
 * set after the sink has left NULL is rejected (playback mode unchanged). The
 * post-NULL leg needs the sink to reach READY, which requires a backend; where no
 * live server is present it is reported skipped rather than failed.
 */
UT_ADD_TEST(L1PropertyTests, AudioSinkWebAudioSettableOnlyInNull)
{
    CONFORMANCE_CORE_TEST();
    GstElement *sink = gst_element_factory_make(kAudioSink, nullptr);
    UT_ASSERT_NOT_NULL_FATAL(sink);
    gboolean value = FALSE;

    // NULL state: web-audio is honoured both ways.
    g_object_set(sink, "web-audio", TRUE, nullptr);
    g_object_get(sink, "web-audio", &value, nullptr);
    UT_ASSERT_TRUE(value);
    g_object_set(sink, "web-audio", FALSE, nullptr);
    g_object_get(sink, "web-audio", &value, nullptr);
    UT_ASSERT_FALSE(value);

    // Leaving NULL must lock the mode: a later set is rejected.
    if (gst_element_set_state(sink, GST_STATE_READY) == GST_STATE_CHANGE_SUCCESS)
    {
        g_object_set(sink, "web-audio", TRUE, nullptr);
        g_object_get(sink, "web-audio", &value, nullptr);
        UT_ASSERT_FALSE(value); // ignored past NULL — mode stays as it was
        gst_element_set_state(sink, GST_STATE_NULL);
    }
    else
    {
        UT_LOG("[mseprop-004] sink could not reach READY without a live server; "
               "post-NULL rejection not exercised on this platform");
    }

    gst_object_unref(sink);
}

/**
 * RC-CORE-MSEPROP-005 — audio-sink server-conditional properties: installed iff
 * the server reports them. Each is asserted only when installed; the documented
 * type/flags/range must hold.
 */
UT_ADD_TEST(L1PropertyTests, AudioSinkConditionalProperties)
{
    CONFORMANCE_CORE_TEST();
    GstElement *sink = gst_element_factory_make(kAudioSink, nullptr);
    UT_ASSERT_NOT_NULL_FATAL(sink);

    assertBoolIfPresent(sink, "low-latency", FALSE, /*R*/ false, /*W*/ true);
    assertBoolIfPresent(sink, "sync", FALSE, /*R*/ true, /*W*/ true);
    assertBoolIfPresent(sink, "sync-off", FALSE, /*R*/ false, /*W*/ true);

    if (hasProp(sink, "stream-sync-mode"))
        assertInt(sink, "stream-sync-mode", 0, G_MAXINT, 0, /*R*/ true, /*W*/ true);
    if (hasProp(sink, "audio-fade"))
        assertString(sink, "audio-fade", /*R*/ false, /*W*/ true);
    if (hasProp(sink, "fade-volume"))
        assertUint(sink, "fade-volume", 0, 100, 0, /*R*/ true, /*W*/ false);
    if (hasProp(sink, "limit-buffering-ms"))
        assertUint(sink, "limit-buffering-ms", 0, 20000, 750, /*R*/ true, /*W*/ true);

    gst_object_unref(sink);
}

/**
 * RC-CORE-MSEPROP-006 — video-sink properties exist with the documented
 * types/defaults/flags/ranges.
 */
UT_ADD_TEST(L1PropertyTests, VideoSinkProperties)
{
    CONFORMANCE_CORE_TEST();
    GstElement *sink = gst_element_factory_make(kVideoSink, nullptr);
    UT_ASSERT_NOT_NULL_FATAL(sink);

    assertString(sink, "rectangle", /*R*/ true, /*W*/ true);
    assertUint(sink, "max-video-width", 0, 3840, 3840, true, true);
    assertUint(sink, "max-video-height", 0, 2160, 2160, true, true);
    assertBool(sink, "frame-step-on-preroll", FALSE, true, true);
    assertBool(sink, "is-master", TRUE, /*R*/ true, /*W*/ false); // installed default is TRUE
    assertInt64(sink, "video_pts", G_MININT64, G_MAXINT64, 0, /*R*/ true, /*W*/ false);

    gst_object_unref(sink);
}

/**
 * RC-CORE-MSEPROP-007 — the deprecated maxVideoWidth/maxVideoHeight aliases exist
 * and map to the canonical max-video-width/height (a set on the alias is read
 * back on the canonical name, and vice-versa). The get warns; the warning is a
 * log-side effect and is not asserted here.
 */
UT_ADD_TEST(L1PropertyTests, VideoSinkDeprecatedAliases)
{
    CONFORMANCE_CORE_TEST();
    GstElement *sink = gst_element_factory_make(kVideoSink, nullptr);
    UT_ASSERT_NOT_NULL_FATAL(sink);

    assertUint(sink, "maxVideoWidth", 0, 3840, 3840, true, true);
    assertUint(sink, "maxVideoHeight", 0, 2160, 2160, true, true);

    guint canonical = 0;
    g_object_set(sink, "maxVideoWidth", 1920u, nullptr);
    g_object_get(sink, "max-video-width", &canonical, nullptr);
    UT_ASSERT_EQUAL(canonical, 1920u);

    g_object_set(sink, "maxVideoHeight", 1080u, nullptr);
    g_object_get(sink, "max-video-height", &canonical, nullptr);
    UT_ASSERT_EQUAL(canonical, 1080u);

    // The alias reads back the canonical value it maps onto.
    guint alias = 0;
    g_object_get(sink, "maxVideoWidth", &alias, nullptr);
    UT_ASSERT_EQUAL(alias, 1920u);

    gst_object_unref(sink);
}

/**
 * RC-CORE-MSEPROP-008 — video-sink server-conditional properties: installed iff
 * supported. Each is asserted only when installed.
 */
UT_ADD_TEST(L1PropertyTests, VideoSinkConditionalProperties)
{
    CONFORMANCE_CORE_TEST();
    GstElement *sink = gst_element_factory_make(kVideoSink, nullptr);
    UT_ASSERT_NOT_NULL_FATAL(sink);

    // immediate-output and show-video-window install with default TRUE; the
    // write-only syncmode-streaming installs FALSE (see rialto-gstreamer
    // RialtoGStreamerMSEVideoSink property registration). immediate-output is
    // read/write; the other two are write-only.
    assertBoolIfPresent(sink, "immediate-output", TRUE, /*R*/ true, /*W*/ true);
    assertBoolIfPresent(sink, "syncmode-streaming", FALSE, /*R*/ false, /*W*/ true);
    assertBoolIfPresent(sink, "show-video-window", TRUE, /*R*/ false, /*W*/ true);

    gst_object_unref(sink);
}

/**
 * RC-CORE-MSEPROP-009 — subtitle-sink properties exist with the documented
 * types/defaults/flags/ranges.
 */
UT_ADD_TEST(L1PropertyTests, SubtitleSinkProperties)
{
    CONFORMANCE_CORE_TEST();
    GstElement *sink = gst_element_factory_make(kSubtitleSink, nullptr);
    UT_ASSERT_NOT_NULL_FATAL(sink);

    assertBool(sink, "mute", FALSE, true, true);
    assertString(sink, "text-track-identifier", /*R*/ true, /*W*/ true);
    assertUint(sink, "window-id", 0, 256, 0, true, true);
    assertBool(sink, "async", FALSE, true, true);

    gst_object_unref(sink);
}

/**
 * RC-CORE-MSEPROP-010 — a readable property returns its documented fallback
 * default when the delegate cannot supply a value. On a freshly-created sink with
 * no attached playback delegate, the base readable properties yield their
 * documented fallbacks (single-path-stream=FALSE, streams-number=1, has-drm=TRUE).
 */
UT_ADD_TEST(L1PropertyTests, ReadablePropertiesFallbackDefaults)
{
    CONFORMANCE_CORE_TEST();
    GstElement *sink = gst_element_factory_make(kAudioSink, nullptr);
    UT_ASSERT_NOT_NULL_FATAL(sink);

    gboolean singlePath = TRUE;
    gint streams = 0;
    gboolean hasDrm = FALSE;
    g_object_get(sink, "single-path-stream", &singlePath, nullptr);
    g_object_get(sink, "streams-number", &streams, nullptr);
    g_object_get(sink, "has-drm", &hasDrm, nullptr);

    UT_ASSERT_FALSE(singlePath);
    UT_ASSERT_EQUAL(streams, 1);
    UT_ASSERT_TRUE(hasDrm);

    gst_object_unref(sink);
}
