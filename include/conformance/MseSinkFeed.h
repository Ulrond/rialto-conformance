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

#ifndef RIALTO_CONFORMANCE_MSE_SINK_FEED_H_
#define RIALTO_CONFORMANCE_MSE_SINK_FEED_H_

/**
 * @file MseSinkFeed.h
 *
 * Data-path feed harness for the mseSink interface (MSE GStreamer sink) conformance cases
 * whose contract is a transform applied to the server source through an *active*
 * sink pad — RC-CORE-MSEEVENT-006..011 and the MSEEVENT-012 upstream-forward
 * clause. These cannot be introspected on a standalone READY sink (the pad event
 * function only runs once the source is attached and the backend pipeline is
 * realized), so this harness stands up a real `appsrc ! rialtomseaudiosink`
 * GStreamer pipeline, drives it to PLAYING against the live RialtoServer the gate
 * provides, and exposes the sink and its active sink pad so a case can send an
 * event and observe the documented effect (a bus message, a query answer, an
 * upstream forward).
 *
 * The elementary stream is the same in-process synthesised AAC the native
 * data-path harness uses (MediaFeed.h::generateAacAdtsStream), so a case stays
 * hermetic and needs no network asset. The buffers are pushed into the sink via
 * appsrc on appsrc's own streaming thread, so the sink's internal
 * backpressure (its bounded sample queue) never blocks the test thread.
 */

#include "MediaFeed.h"

#include <chrono>
#include <cstdint>

#include <gst/app/gstappsrc.h>
#include <gst/gst.h>

namespace rialto::conformance
{
/**
 * Drives a real `appsrc ! rialtomseaudiosink` pipeline feeding a synthesised AAC
 * stream through the Rialto MSE audio sink to a live RialtoServer.
 *
 * Lifecycle: construct (builds + links the pipeline), startPlaying() (attach the
 * audio source via CAPS, report one audio stream so the sink sends
 * allSourcesAttached, feed the frames, reach PLAYING), then send events to
 * sinkPad() and observe. Destruction tears the pipeline to NULL.
 */
class MseSinkFeed
{
public:
    /// Build the pipeline for @p stream. Check isValid() before use.
    explicit MseSinkFeed(const AacElementaryStream &stream);
    ~MseSinkFeed();

    MseSinkFeed(const MseSinkFeed &) = delete;
    MseSinkFeed &operator=(const MseSinkFeed &) = delete;

    /// True once the pipeline elements were created and linked.
    bool isValid() const { return m_pipeline != nullptr && m_appsrc != nullptr && m_sink != nullptr; }

    /// The rialtomseaudiosink element (transfer-none, owned by the harness).
    GstElement *sink() const { return m_sink; }

    /// The appsrc feeding the sink — the sink pad's upstream peer, for probing the
    /// upstream-event forward (MSEEVENT-012) (transfer-none, owned by the harness).
    GstElement *source() const { return m_appsrc; }

    /// The sink's static "sink" pad, for sending events and running queries
    /// (transfer-none, owned by the harness).
    GstPad *sinkPad() const { return m_sinkPad; }

    /**
     * Drive the pipeline to PLAYING against the live server: request PLAYING
     * (async), report a single audio stream to the sink (STREAM_COLLECTION, which
     * only succeeds once the sink's media-player client exists — the harness uses
     * that as the client-ready signal), attach the AAC audio source (the CAPS the
     * appsrc pushes) and feed the frames until the server prerolls and reaches
     * PLAYING.
     *
     * @param[in] timeout  upper bound on reaching PLAYING on the headless path.
     * @retval true if the pipeline reached PLAYING within @p timeout.
     */
    bool startPlaying(std::chrono::milliseconds timeout);

    /// Send @p event to the active sink pad (transfer-full of @p event, as
    /// gst_pad_send_event). @retval the sink's handling result.
    bool sendEventToSink(GstEvent *event);

    /**
     * Report an audio/video/text stream collection to the sink (the transform
     * MSEEVENT-011 covers). @retval true if the sink accepted it (its media-player
     * client exists); false if there is no client yet.
     */
    bool reportStreamCollection(int audioStreams, int videoStreams, int textStreams);

    /**
     * Block until a bus message of any type in @p types arrives, up to @p timeout.
     * The instant-rate request (MSEEVENT-006) and reset-time (MSEEVENT-010) the
     * sink posts are dedicated message types filtered directly here.
     * @retval the message (transfer-full, caller unrefs) or nullptr on timeout.
     */
    GstMessage *waitForBusMessage(GstMessageType types, std::chrono::milliseconds timeout);

    /// Signal end-of-stream on the appsrc so the stream drains through the sink.
    void signalEos();

private:
    // appsrc need-data: push the next encoded frame, replaying the frame list with
    // an increasing PTS so the stream never ends (the sink pad stays live, not EOS,
    // so serialized events sent to it are accepted — until signalEos() is called).
    // Runs on appsrc's streaming thread.
    static void needData(GstAppSrc *src, guint length, gpointer user);
    static void enoughData(GstAppSrc *src, gpointer user);
    void pushNextFrame();

    AacElementaryStream m_stream;
    size_t m_nextFrame = 0;
    size_t m_loopCount = 0;      ///< times the frame list has been replayed
    int64_t m_streamDurationNs = 0; ///< PTS offset added per replay loop
    bool m_eosSignalled = false;

    GstElement *m_pipeline = nullptr;
    GstElement *m_appsrc = nullptr;
    GstElement *m_sink = nullptr;
    GstPad *m_sinkPad = nullptr; ///< the sink's static "sink" pad (owned)
    GstBus *m_bus = nullptr;     ///< the pipeline bus (owned)
};

} // namespace rialto::conformance

#endif // RIALTO_CONFORMANCE_MSE_SINK_FEED_H_
