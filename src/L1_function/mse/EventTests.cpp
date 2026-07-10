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
 * @file EventTests.cpp
 *
 * L1 — function testing for Surface A: the GStreamer query/event surface of
 * RialtoMSEBaseSink. An external media app queries these sinks (seeking,
 * position, duration, segment) and sends them events (seeks, custom events)
 * through the standard GstElement API, so the suite drives the same surface via
 * gst_element_query / gst_element_send_event and asserts the documented answer.
 * No Rialto internal headers are involved (§2 Surface A); the base-sink behaviour
 * is exercised on the audio sink as a representative concrete instance.
 *
 * This is the introspection-reachable subset — the answers a sink gives before a
 * source is attached (no client, or a value the sink owns). The transitions that
 * apply an event to the server source (CAPS/EOS/SEGMENT/FLUSH/STREAM_COLLECTION,
 * MSEEVENT-006..011) are data-flow driven and run on an active sink pad in
 * src/L4_e2e/mse/SinkEventDataPathTests.cpp.
 *
 * Coverage trace: coverage/rc-core-catalog.yaml — RC-CORE-MSEEVENT-001,002,003,
 * 004,005, and the 012 downstream-consume clause.
 */

#include <ut.h>

#include "conformance/Surfaces.h"
#include "conformance/TierGate.h"

#include <gst/gst.h>

using rialto::conformance::MseSinkSurface;

namespace
{
class L1EventTests : public MseSinkSurface
{
};

// A fresh audio sink brought to READY, where the delegate exists (so it answers
// queries/events) but no source is attached (so no media-player client).
GstElement *makeReadyAudioSink()
{
    GstElement *sink = gst_element_factory_make("rialtomseaudiosink", nullptr);
    UT_ASSERT_NOT_NULL(sink);
    if (sink != nullptr)
        UT_ASSERT_EQUAL(gst_element_set_state(sink, GST_STATE_READY), GST_STATE_CHANGE_SUCCESS);
    return sink;
}

void teardown(GstElement *sink)
{
    gst_element_set_state(sink, GST_STATE_NULL);
    gst_object_unref(sink);
}

} // namespace

UT_ADD_TEST_TO_GROUP(L1EventTests, UT_TESTS_L1);

// The MSE sink query/event surface is part of the required Surface A, tested
// unconditionally against the running server the gate provides.

/**
 * RC-CORE-MSEEVENT-001 — GST_QUERY_SEEKING answers seekable=FALSE with range
 * [0, -1]: Rialto sinks are not seekable through the GStreamer seek-query surface.
 */
UT_ADD_TEST(L1EventTests, SeekingQueryReportsNotSeekable)
{
    CONFORMANCE_CORE_TEST();
    GstElement *sink = makeReadyAudioSink();
    UT_ASSERT_NOT_NULL_FATAL(sink);

    GstQuery *query = gst_query_new_seeking(GST_FORMAT_TIME);
    UT_ASSERT_TRUE(gst_element_query(sink, query));

    GstFormat fmt = GST_FORMAT_UNDEFINED;
    gboolean seekable = TRUE;
    gint64 start = -1, end = 0;
    gst_query_parse_seeking(query, &fmt, &seekable, &start, &end);
    UT_ASSERT_EQUAL(fmt, GST_FORMAT_TIME);
    UT_ASSERT_FALSE(seekable);
    UT_ASSERT_EQUAL(start, static_cast<gint64>(0));
    UT_ASSERT_EQUAL(end, static_cast<gint64>(-1));

    gst_query_unref(query);
    teardown(sink);
}

/**
 * RC-CORE-MSEEVENT-002 — GST_QUERY_POSITION returns FALSE when there is no
 * media-player client (no source attached yet).
 */
UT_ADD_TEST(L1EventTests, PositionQueryFalseWithoutClient)
{
    CONFORMANCE_CORE_TEST();
    GstElement *sink = makeReadyAudioSink();
    UT_ASSERT_NOT_NULL_FATAL(sink);

    GstQuery *query = gst_query_new_position(GST_FORMAT_TIME);
    UT_ASSERT_FALSE(gst_element_query(sink, query));

    gst_query_unref(query);
    teardown(sink);
}

/**
 * RC-CORE-MSEEVENT-003 — GST_QUERY_DURATION returns FALSE when there is no
 * media-player client (no source attached yet).
 */
UT_ADD_TEST(L1EventTests, DurationQueryFalseWithoutClient)
{
    CONFORMANCE_CORE_TEST();
    GstElement *sink = makeReadyAudioSink();
    UT_ASSERT_NOT_NULL_FATAL(sink);

    GstQuery *query = gst_query_new_duration(GST_FORMAT_TIME);
    UT_ASSERT_FALSE(gst_element_query(sink, query));

    gst_query_unref(query);
    teardown(sink);
}

/**
 * RC-CORE-MSEEVENT-004 — GST_QUERY_SEGMENT returns the last segment's rate and
 * format with stream-time start/stop. On a sink that has seen no segment, this is
 * the initialised TIME segment (rate 1.0, start 0, stop unset/-1).
 */
UT_ADD_TEST(L1EventTests, SegmentQueryReturnsLastSegment)
{
    CONFORMANCE_CORE_TEST();
    GstElement *sink = makeReadyAudioSink();
    UT_ASSERT_NOT_NULL_FATAL(sink);

    GstQuery *query = gst_query_new_segment(GST_FORMAT_TIME);
    UT_ASSERT_TRUE(gst_element_query(sink, query));

    gdouble rate = 0.0;
    GstFormat fmt = GST_FORMAT_UNDEFINED;
    gint64 start = -1, stop = 0;
    gst_query_parse_segment(query, &rate, &fmt, &start, &stop);
    UT_ASSERT_DOUBLE_EQUAL(rate, 1.0, 1e-9);
    UT_ASSERT_EQUAL(fmt, GST_FORMAT_TIME);
    UT_ASSERT_EQUAL(start, static_cast<gint64>(0));
    UT_ASSERT_EQUAL(stop, static_cast<gint64>(-1));

    gst_query_unref(query);
    teardown(sink);
}

/**
 * RC-CORE-MSEEVENT-005 — only FLUSH seeks are supported: a FLUSH seek with
 * SEEK_TYPE_END in TIME is rejected, and a non-FLUSH seek is rejected. Both are
 * refused (send_event returns FALSE) rather than applied.
 */
UT_ADD_TEST(L1EventTests, UnsupportedSeeksAreRejected)
{
    CONFORMANCE_CORE_TEST();
    GstElement *sink = makeReadyAudioSink();
    UT_ASSERT_NOT_NULL_FATAL(sink);

    GstEvent *endSeek = gst_event_new_seek(1.0, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH, GST_SEEK_TYPE_END, 0,
                                           GST_SEEK_TYPE_NONE, 0);
    UT_ASSERT_FALSE(gst_element_send_event(sink, endSeek));

    GstEvent *nonFlushSeek = gst_event_new_seek(1.0, GST_FORMAT_TIME, GST_SEEK_FLAG_NONE, GST_SEEK_TYPE_SET, 0,
                                                GST_SEEK_TYPE_NONE, 0);
    UT_ASSERT_FALSE(gst_element_send_event(sink, nonFlushSeek));

    teardown(sink);
}

/**
 * RC-CORE-MSEEVENT-012 — an unhandled event that the sink does not act on is
 * consumed: a custom downstream event the sink has no handler for is swallowed
 * and send_event returns TRUE (it is not propagated as an error). The companion
 * clause — an unknown *upstream* event forwarded to the upstream peer — needs an
 * active sink pad in a linked pipeline, so it runs in
 * src/L4_e2e/mse/SinkEventDataPathTests.cpp (UnknownUpstreamEventForwardedUpstream).
 */
UT_ADD_TEST(L1EventTests, UnhandledDownstreamEventIsConsumed)
{
    CONFORMANCE_CORE_TEST();
    GstElement *sink = makeReadyAudioSink();
    UT_ASSERT_NOT_NULL_FATAL(sink);

    GstEvent *downstream =
        gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM, gst_structure_new_empty("rialto-conformance-downstream"));
    UT_ASSERT_TRUE(gst_element_send_event(sink, downstream));

    teardown(sink);
}
