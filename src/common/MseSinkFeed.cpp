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
 * @file MseSinkFeed.cpp
 *
 * Implementation of the mseSink interface MSE-sink data-path feed harness (MseSinkFeed.h):
 * a real `appsrc ! rialtomseaudiosink` pipeline that feeds synthesised AAC through
 * the Rialto MSE audio sink to the live RialtoServer.
 */

#include "conformance/MseSinkFeed.h"

#include <gst/app/gstappsrc.h>

#include <thread>

namespace rialto::conformance
{
namespace
{
// A small appsrc queue: enough to preroll, but small so that after signalEos() the
// buffered backlog drains to EOS quickly (the sink applies its own bounded
// backpressure downstream, so this only bounds appsrc's own hold).
constexpr guint64 kAppsrcMaxBytes = 16 * 1024;

// Poll interval while waiting for the sink's media-player client to come up
// (the client connects to the server asynchronously during the state change).
constexpr std::chrono::milliseconds kClientPoll{20};
} // namespace

MseSinkFeed::MseSinkFeed(const AacElementaryStream &stream)
    : m_stream(stream), m_streamDurationNs(stream.totalDurationNs())
{
    if (!gst_is_initialized())
        gst_init(nullptr, nullptr);

    m_pipeline = gst_pipeline_new("rialto-conformance-mse-feed");
    m_appsrc = gst_element_factory_make("appsrc", "src");
    m_sink = gst_element_factory_make("rialtomseaudiosink", "sink");
    if (m_pipeline == nullptr || m_appsrc == nullptr || m_sink == nullptr)
        return;

    gst_bin_add_many(GST_BIN(m_pipeline), m_appsrc, m_sink, nullptr);
    if (!gst_element_link(m_appsrc, m_sink))
    {
        // Leave the elements owned by the bin; isValid() still true but startPlaying
        // will fail to preroll. Surfacing the link failure is the caller's job.
        GST_ERROR_OBJECT(m_pipeline, "Failed to link appsrc -> rialtomseaudiosink");
    }

    // The encoded ADTS AAC the appsrc advertises; the sink maps these caps to a
    // Rialto audio source and the server's aacparse configures the decoder from the
    // self-describing ADTS frames.
    GstCaps *caps = gst_caps_new_simple("audio/mpeg", "mpegversion", G_TYPE_INT, 4, "stream-format", G_TYPE_STRING,
                                        "adts", "rate", G_TYPE_INT, static_cast<gint>(m_stream.sampleRate), "channels",
                                        G_TYPE_INT, static_cast<gint>(m_stream.channels), nullptr);
    g_object_set(m_appsrc, "caps", caps, "format", GST_FORMAT_TIME, "stream-type", GST_APP_STREAM_TYPE_STREAM,
                 "is-live", FALSE, "max-bytes", kAppsrcMaxBytes, "block", FALSE, nullptr);
    gst_caps_unref(caps);

    GstAppSrcCallbacks callbacks{};
    callbacks.need_data = &MseSinkFeed::needData;
    callbacks.enough_data = &MseSinkFeed::enoughData;
    gst_app_src_set_callbacks(GST_APP_SRC(m_appsrc), &callbacks, this, nullptr);

    m_sinkPad = gst_element_get_static_pad(m_sink, "sink");
    m_bus = gst_pipeline_get_bus(GST_PIPELINE(m_pipeline));
}

MseSinkFeed::~MseSinkFeed()
{
    if (m_pipeline != nullptr)
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
    if (m_sinkPad != nullptr)
        gst_object_unref(m_sinkPad);
    if (m_bus != nullptr)
        gst_object_unref(m_bus);
    if (m_pipeline != nullptr)
        gst_object_unref(m_pipeline); // drops the bin and its elements
}

void MseSinkFeed::needData(GstAppSrc * /*src*/, guint /*length*/, gpointer user)
{
    static_cast<MseSinkFeed *>(user)->pushNextFrame();
}

void MseSinkFeed::enoughData(GstAppSrc * /*src*/, gpointer /*user*/) {}

void MseSinkFeed::pushNextFrame()
{
    if (m_eosSignalled || m_stream.frames.empty())
        return;

    // Replay the frame list indefinitely so the source never ends on its own; a
    // per-loop PTS offset keeps timestamps monotonic. The pipeline stays PLAYING
    // and the sink pad stays live until a case explicitly calls signalEos().
    if (m_nextFrame >= m_stream.frames.size())
    {
        m_nextFrame = 0;
        ++m_loopCount;
    }

    const EncodedFrame &frame = m_stream.frames[m_nextFrame++];
    const int64_t pts = frame.timeStamp + static_cast<int64_t>(m_loopCount) * m_streamDurationNs;
    GstBuffer *buffer = gst_buffer_new_allocate(nullptr, frame.data.size(), nullptr);
    gst_buffer_fill(buffer, 0, frame.data.data(), frame.data.size());
    GST_BUFFER_PTS(buffer) = static_cast<GstClockTime>(pts);
    GST_BUFFER_DURATION(buffer) = static_cast<GstClockTime>(frame.duration);

    // push-buffer takes ownership; it drives the sink chain on this (appsrc) thread,
    // so the sink's bounded-queue backpressure never blocks the test thread.
    gst_app_src_push_buffer(GST_APP_SRC(m_appsrc), buffer);
}

void MseSinkFeed::signalEos()
{
    if (!m_eosSignalled && m_appsrc != nullptr)
    {
        m_eosSignalled = true;
        gst_app_src_end_of_stream(GST_APP_SRC(m_appsrc));
    }
}

bool MseSinkFeed::sendEventToSink(GstEvent *event)
{
    if (m_sinkPad == nullptr)
    {
        gst_event_unref(event);
        return false;
    }
    return gst_pad_send_event(m_sinkPad, event);
}

bool MseSinkFeed::reportStreamCollection(int audioStreams, int videoStreams, int textStreams)
{
    GstStreamCollection *collection = gst_stream_collection_new("rialto-conformance");
    auto addStreams = [&](int count, GstStreamType type, const char *prefix)
    {
        for (int i = 0; i < count; ++i)
        {
            gchar *id = g_strdup_printf("%s-%d", prefix, i);
            GstStream *stream = gst_stream_new(id, nullptr, type, GST_STREAM_FLAG_NONE);
            gst_stream_collection_add_stream(collection, stream); // transfer-full of stream
            g_free(id);
        }
    };
    addStreams(audioStreams, GST_STREAM_TYPE_AUDIO, "audio");
    addStreams(videoStreams, GST_STREAM_TYPE_VIDEO, "video");
    addStreams(textStreams, GST_STREAM_TYPE_TEXT, "text");

    // gst_event_new_stream_collection is transfer-none of the collection.
    GstEvent *event = gst_event_new_stream_collection(collection);
    gst_object_unref(collection);
    return sendEventToSink(event);
}

bool MseSinkFeed::startPlaying(std::chrono::milliseconds timeout)
{
    if (!isValid())
        return false;

    const auto deadline = std::chrono::steady_clock::now() + timeout;

    // Request PLAYING; the sink creates its media-player client, connects to the
    // server, loads the MSE session, and prerolls once fed. This returns ASYNC
    // (preroll pending) on the software render path.
    GstStateChangeReturn ret = gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE)
        return false;

    // Report one audio stream so the sink can send allSourcesAttached. The
    // STREAM_COLLECTION handler only succeeds once the media-player client exists,
    // so retrying until it succeeds is also the client-ready signal. The CAPS the
    // appsrc pushes attaches the source; whichever of the two lands last triggers
    // allSourcesAttached (both re-check the attached/expected stream counts).
    bool reported = false;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (reportStreamCollection(1, 0, 0))
        {
            reported = true;
            break;
        }
        std::this_thread::sleep_for(kClientPoll);
    }
    if (!reported)
        return false;

    // Wait out the async preroll to PLAYING (or an error) within the remaining
    // budget. appsrc keeps feeding on its own thread meanwhile.
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline)
        return false;
    const auto remainingNs =
        std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - now).count();

    GstState state = GST_STATE_NULL;
    GstState pending = GST_STATE_NULL;
    ret = gst_element_get_state(m_pipeline, &state, &pending, static_cast<GstClockTime>(remainingNs));
    return ret == GST_STATE_CHANGE_SUCCESS && state == GST_STATE_PLAYING;
}

GstMessage *MseSinkFeed::waitForBusMessage(GstMessageType types, std::chrono::milliseconds timeout)
{
    if (m_bus == nullptr)
        return nullptr;
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(timeout).count();
    return gst_bus_timed_pop_filtered(m_bus, static_cast<GstClockTime>(ns), types);
}

} // namespace rialto::conformance
