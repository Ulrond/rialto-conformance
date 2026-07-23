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
 * @file SinkEventDataPathTests.cpp
 *
 * L4 — end-to-end data-path cases for mseSink interface (the RialtoMSEBaseSink GStreamer
 * event surface), the counterpart to the introspection-reachable L1 EventTests.
 *
 * The MSEEVENT-006..011 contracts are transforms a sink applies to the *server
 * source* through an ACTIVE sink pad — they only run once the source is attached
 * and the backend pipeline is realized, so they cannot be exercised on a
 * standalone READY sink. Each case here stands up a real `appsrc !
 * rialtomseaudiosink` pipeline (MseSinkFeed), drives it to PLAYING against the
 * live RialtoServer feeding synthesised AAC, then sends the event and observes the
 * documented effect through the public GStreamer surface (a bus message, a query
 * answer, or an upstream forward). No Rialto internal headers are involved.
 *
 * Coverage trace: coverage/rc-core-catalog.yaml / matrix.yaml — RC-CORE-MSEEVENT-006
 * (instant-rate-change), 007 (SEGMENT applied), 008 (EOS), 009 (CAPS attaches
 * source), 010 (FLUSH honouring reset-time), 011 (STREAM_COLLECTION triggers
 * all-sources-attached), and the 012 upstream-forward clause.
 */

#include <ut.h>
#include <ut_log.h>

#include "conformance/MseSinkFeed.h"
#include "conformance/Surfaces.h"
#include "conformance/TierGate.h"

#include <gst/gst.h>

#include <chrono>

using rialto::conformance::AacElementaryStream;
using rialto::conformance::generateAacAdtsStream;
using rialto::conformance::MseSinkFeed;
using rialto::conformance::MseSinkSurface;

namespace
{
// AAC access units to synthesise (~1s at 48 kHz) — enough for the server to reach
// and hold PLAYING while the event under test is applied.
constexpr int kAacFrames = 50;

// Upper bound on reaching PLAYING through the MSE sink on the headless software
// render path (the native data path uses the same ceiling).
constexpr std::chrono::milliseconds kPlayingTimeout{15000};

// Upper bound on a single bus message posted in response to an applied event.
constexpr std::chrono::milliseconds kMessageTimeout{10000};

class L4SinkEventDataPathTests : public MseSinkSurface
{
};

// Bring a fed MSE-sink pipeline to PLAYING or fail fatally: baseline codec support
// is required, so a synthesis/preroll failure is a platform fault, not a soft skip.
#define ARRANGE_PLAYING_FEED(feed)                                                                                      \
    AacElementaryStream stream = generateAacAdtsStream(kAacFrames);                                                     \
    UT_LOG("[mse-datapath] synthesised AAC frames=%zu rate=%u channels=%u", stream.frames.size(), stream.sampleRate,   \
           stream.channels);                                                                                            \
    UT_ASSERT_TRUE_FATAL(!stream.frames.empty());                                                                       \
    MseSinkFeed feed(stream);                                                                                           \
    UT_ASSERT_TRUE_FATAL(feed.isValid());                                                                               \
    const bool reachedPlaying = feed.startPlaying(kPlayingTimeout);                                                     \
    UT_LOG("[mse-datapath] startPlaying reached PLAYING: %d", reachedPlaying);                                          \
    UT_ASSERT_TRUE_FATAL(reachedPlaying)

// Upstream-event probe state for MSEEVENT-012: flags when the named custom event
// passes the appsrc src pad (i.e. the sink forwarded it upstream).
struct UpstreamProbe
{
    const char *name = nullptr;
    bool seen = false;
};

GstPadProbeReturn upstreamEventProbe(GstPad * /*pad*/, GstPadProbeInfo *info, gpointer user)
{
    auto *probe = static_cast<UpstreamProbe *>(user);
    GstEvent *event = GST_PAD_PROBE_INFO_EVENT(info);
    if (event != nullptr && gst_event_has_name(event, probe->name))
        probe->seen = true;
    return GST_PAD_PROBE_OK;
}
} // namespace

UT_ADD_TEST_TO_GROUP(L4SinkEventDataPathTests, UT_TESTS_L4);

// The MSE sink event surface is part of the required mseSink interface, tested
// unconditionally against the running server the gate provides.

/**
 * RC-CORE-MSEEVENT-009 — GST_EVENT_CAPS stores the source caps and triggers source
 * attach. The appsrc pushes the AAC caps into the sink; only if the sink creates
 * and attaches the audio source from them does the server realize its pipeline,
 * preroll the fed frames, and reach PLAYING. Reaching PLAYING is therefore the
 * proof of the attach; the position query then answering TRUE (it answers FALSE
 * with no attached source — see L1 MSEEVENT-002) confirms the source is live.
 */
UT_ADD_TEST(L4SinkEventDataPathTests, CapsAttachesSourceReachingPlaying)
{
    CONFORMANCE_CORE_TEST();
    ARRANGE_PLAYING_FEED(feed);

    GstQuery *query = gst_query_new_position(GST_FORMAT_TIME);
    const bool positionAnswered = gst_element_query(feed.sink(), query);
    gint64 position = -1;
    if (positionAnswered)
        gst_query_parse_position(query, nullptr, &position);
    UT_LOG("[mse-datapath] position query once playing: answered=%d pos=%lld", positionAnswered,
           static_cast<long long>(position));
    gst_query_unref(query);

    UT_ASSERT_TRUE(positionAnswered);
    UT_ASSERT_TRUE(position >= 0);
}

/**
 * RC-CORE-MSEEVENT-011 — GST_EVENT_STREAM_COLLECTION counts the a/v/text streams,
 * reports the collection to the server, and triggers all-sources-attached. The
 * harness relies on exactly this to reach PLAYING (a single audio stream is
 * reported so the sink sends allSourcesAttached), so reaching PLAYING proves the
 * all-sources-attached trigger. A further collection is accepted (TRUE) because the
 * sink's media-player client exists — the handler rejects a collection (FALSE)
 * only when there is no client.
 */
UT_ADD_TEST(L4SinkEventDataPathTests, StreamCollectionTriggersAllSourcesAttached)
{
    CONFORMANCE_CORE_TEST();
    ARRANGE_PLAYING_FEED(feed);

    const bool accepted = feed.reportStreamCollection(1, 0, 0);
    UT_LOG("[mse-datapath] stream-collection accepted with live client: %d", accepted);
    UT_ASSERT_TRUE(accepted);
}

/**
 * RC-CORE-MSEEVENT-007 — GST_EVENT_SEGMENT is copied and applied to the server
 * source. A TIME segment with a distinctive stop is sent to the active sink pad;
 * the sink stores it as its last segment, so the SEGMENT query then reports that
 * stop (the fresh sink's segment has an unset stop of -1 — see L1 MSEEVENT-004).
 */
UT_ADD_TEST(L4SinkEventDataPathTests, SegmentEventAppliedToServerSource)
{
    CONFORMANCE_CORE_TEST();
    ARRANGE_PLAYING_FEED(feed);

    // A distinctive stop well past the fed timeline (the fresh sink's segment has
    // an unset stop of -1), so the applied value is unambiguous in the query.
    constexpr gint64 kStop = 3600 * GST_SECOND;
    GstSegment segment;
    gst_segment_init(&segment, GST_FORMAT_TIME);
    segment.start = 0;
    segment.stop = kStop;
    segment.rate = 1.0;
    UT_ASSERT_TRUE(feed.sendEventToSink(gst_event_new_segment(&segment)));

    GstQuery *query = gst_query_new_segment(GST_FORMAT_TIME);
    UT_ASSERT_TRUE(gst_element_query(feed.sink(), query));
    gdouble rate = 0.0;
    GstFormat fmt = GST_FORMAT_UNDEFINED;
    gint64 start = -1, stop = -1;
    gst_query_parse_segment(query, &rate, &fmt, &start, &stop);
    UT_LOG("[mse-datapath] segment query after applied SEGMENT: rate=%f fmt=%d start=%lld stop=%lld", rate, fmt,
           static_cast<long long>(start), static_cast<long long>(stop));
    gst_query_unref(query);

    UT_ASSERT_EQUAL(fmt, GST_FORMAT_TIME);
    UT_ASSERT_DOUBLE_EQUAL(rate, 1.0, 1e-9);
    UT_ASSERT_EQUAL(stop, kStop);
}

/**
 * RC-CORE-MSEEVENT-008 — GST_EVENT_EOS marks end-of-stream and notifies the
 * flush/data synchronizer. Signalling EOS on the appsrc sends GST_EVENT_EOS to the
 * sink, which marks end-of-stream; once the server drains the fed data, the
 * pipeline posts EOS on its bus.
 */
UT_ADD_TEST(L4SinkEventDataPathTests, EosEventReachesEndOfStream)
{
    CONFORMANCE_CORE_TEST();
    ARRANGE_PLAYING_FEED(feed);

    feed.signalEos();
    GstMessage *msg = feed.waitForBusMessage(GST_MESSAGE_EOS, kPlayingTimeout);
    const bool sawEos = (msg != nullptr);
    UT_LOG("[mse-datapath] EOS posted on the bus: %d", sawEos);
    if (msg != nullptr)
        gst_message_unref(msg);
    UT_ASSERT_TRUE(sawEos);
}

/**
 * RC-CORE-MSEEVENT-010 — GST_EVENT_FLUSH_START / FLUSH_STOP begin and stop
 * flushing, honouring the reset-time flag. FLUSH_START begins flushing on the
 * active sink pad; FLUSH_STOP with reset-time TRUE stops it and posts a reset-time
 * message on the pipeline bus.
 */
UT_ADD_TEST(L4SinkEventDataPathTests, FlushHonoursResetTime)
{
    CONFORMANCE_CORE_TEST();
    ARRANGE_PLAYING_FEED(feed);

    feed.sendEventToSink(gst_event_new_flush_start());
    feed.sendEventToSink(gst_event_new_flush_stop(TRUE));

    GstMessage *msg = feed.waitForBusMessage(GST_MESSAGE_RESET_TIME, kMessageTimeout);
    const bool sawResetTime = (msg != nullptr);
    UT_LOG("[mse-datapath] reset-time posted after FLUSH_STOP(reset=TRUE): %d", sawResetTime);
    if (msg != nullptr)
        gst_message_unref(msg);
    UT_ASSERT_TRUE(sawResetTime);
}

/**
 * RC-CORE-MSEEVENT-006 — a FLUSH seek carrying the instant-rate-change flag is
 * translated to an instant-rate-change event applied to the sink pad, which posts
 * an instant-rate-request message on the pipeline bus (the pipeline then issues
 * INSTANT_RATE_SYNC_TIME, applying setPlaybackRate). The request message is the
 * observable proof the seek flag was translated and applied to the source.
 */
UT_ADD_TEST(L4SinkEventDataPathTests, InstantRateChangeSeekPostsRateRequest)
{
    CONFORMANCE_CORE_TEST();
    ARRANGE_PLAYING_FEED(feed);

    GstEvent *seek =
        gst_event_new_seek(2.0, GST_FORMAT_TIME, static_cast<GstSeekFlags>(GST_SEEK_FLAG_INSTANT_RATE_CHANGE),
                           GST_SEEK_TYPE_NONE, 0, GST_SEEK_TYPE_NONE, 0);
    // The observable contract is the instant-rate-request the sink posts on the bus
    // (the pipeline then issues INSTANT_RATE_SYNC_TIME); the send_event return value
    // is not part of it, so it is logged rather than asserted.
    const bool sent = gst_element_send_event(feed.sink(), seek);

    GstMessage *msg = feed.waitForBusMessage(GST_MESSAGE_INSTANT_RATE_REQUEST, kMessageTimeout);
    const bool sawRequest = (msg != nullptr);
    UT_LOG("[mse-datapath] instant-rate seek send=%d, instant-rate-request posted=%d", sent, sawRequest);
    if (msg != nullptr)
        gst_message_unref(msg);
    UT_ASSERT_TRUE(sawRequest);
}

/**
 * RC-CORE-MSEEVENT-012 (upstream clause) — an unknown upstream event is forwarded
 * upstream. A custom upstream event sent to the sink is pushed on through the
 * active sink pad to its upstream peer (the appsrc), where a pad probe observes it.
 * (The companion "unhandled downstream event is consumed" clause is covered on a
 * standalone sink by L1 MSEEVENT-012.)
 */
UT_ADD_TEST(L4SinkEventDataPathTests, UnknownUpstreamEventForwardedUpstream)
{
    CONFORMANCE_CORE_TEST();
    ARRANGE_PLAYING_FEED(feed);

    constexpr const char *kName = "rialto-conformance-upstream";
    GstPad *srcPad = gst_element_get_static_pad(feed.source(), "src");
    UT_ASSERT_NOT_NULL_FATAL(srcPad);

    UpstreamProbe probe{kName, false};
    gulong probeId = gst_pad_add_probe(srcPad, GST_PAD_PROBE_TYPE_EVENT_UPSTREAM, upstreamEventProbe, &probe, nullptr);

    GstEvent *upstream = gst_event_new_custom(GST_EVENT_CUSTOM_UPSTREAM, gst_structure_new_empty(kName));
    const bool forwarded = gst_element_send_event(feed.sink(), upstream);
    UT_LOG("[mse-datapath] custom upstream event send result=%d observed-at-peer=%d", forwarded, probe.seen);

    gst_pad_remove_probe(srcPad, probeId);
    gst_object_unref(srcPad);

    UT_ASSERT_TRUE(probe.seen);
}
