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
 * @file MseVideoSinkFeed.cpp
 *
 * Implementation of the Surface A MSE video-sink data-path feed harness
 * (MseVideoSinkFeed.h): a real `appsrc ! rialtomsevideosink` pipeline that feeds a
 * synthesised AVC-form H.264 stream through the Rialto MSE video sink to the live
 * RialtoServer. The appsrc caps carry the codec_data / stream-format / alignment
 * fields whose parse RC-CORE-MSECAPS-006 asserts.
 */

#include "conformance/MseVideoSinkFeed.h"

#include <gst/app/gstappsrc.h>

#include <thread>

namespace rialto::conformance
{
namespace
{
// A small appsrc queue: enough to preroll, but small so that after signalEos() the
// buffered backlog drains to EOS quickly (the sink applies its own bounded
// backpressure downstream, so this only bounds appsrc's own hold).
constexpr guint64 kAppsrcMaxBytes = 64 * 1024;

// Poll interval while waiting for the sink's media-player client to come up.
constexpr std::chrono::milliseconds kClientPoll{20};

// The constant frame rate the synthesiser used, echoed onto the appsrc caps.
constexpr int kVideoFrameRate = 25;
} // namespace

MseVideoSinkFeed::MseVideoSinkFeed(const H264AvcElementaryStream &stream)
    : m_stream(stream), m_streamDurationNs(stream.totalDurationNs())
{
    if (!gst_is_initialized())
        gst_init(nullptr, nullptr);

    m_pipeline = gst_pipeline_new("rialto-conformance-mse-video-feed");
    m_appsrc = gst_element_factory_make("appsrc", "src");
    m_sink = gst_element_factory_make("rialtomsevideosink", "sink");
    if (m_pipeline == nullptr || m_appsrc == nullptr || m_sink == nullptr)
        return;

    gst_bin_add_many(GST_BIN(m_pipeline), m_appsrc, m_sink, nullptr);
    if (!gst_element_link(m_appsrc, m_sink))
    {
        GST_ERROR_OBJECT(m_pipeline, "Failed to link appsrc -> rialtomsevideosink");
    }

    // The AVC-form H.264 caps the appsrc advertises: stream-format=avc + alignment=au
    // + the out-of-band codec_data (avcC). These are exactly the fields the sink's
    // createMediaSource parses (RC-CORE-MSECAPS-006); without the codec_data the
    // server's decoder cannot configure an AVC stream, so it must reach the source.
    GstBuffer *codecData =
        gst_buffer_new_allocate(nullptr, m_stream.codecData.size(), nullptr);
    gst_buffer_fill(codecData, 0, m_stream.codecData.data(), m_stream.codecData.size());
    GstCaps *caps = gst_caps_new_simple("video/x-h264", "stream-format", G_TYPE_STRING, "avc", "alignment", G_TYPE_STRING,
                                        "au", "width", G_TYPE_INT, static_cast<gint>(m_stream.width), "height",
                                        G_TYPE_INT, static_cast<gint>(m_stream.height), "framerate", GST_TYPE_FRACTION,
                                        kVideoFrameRate, 1, "codec_data", GST_TYPE_BUFFER, codecData, nullptr);
    g_object_set(m_appsrc, "caps", caps, "format", GST_FORMAT_TIME, "stream-type", GST_APP_STREAM_TYPE_STREAM, "is-live",
                 FALSE, "max-bytes", kAppsrcMaxBytes, "block", FALSE, nullptr);
    gst_caps_unref(caps);
    gst_buffer_unref(codecData);

    GstAppSrcCallbacks callbacks{};
    callbacks.need_data = &MseVideoSinkFeed::needData;
    callbacks.enough_data = &MseVideoSinkFeed::enoughData;
    gst_app_src_set_callbacks(GST_APP_SRC(m_appsrc), &callbacks, this, nullptr);

    m_sinkPad = gst_element_get_static_pad(m_sink, "sink");
}

MseVideoSinkFeed::~MseVideoSinkFeed()
{
    if (m_pipeline != nullptr)
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
    if (m_sinkPad != nullptr)
        gst_object_unref(m_sinkPad);
    if (m_pipeline != nullptr)
        gst_object_unref(m_pipeline); // drops the bin and its elements
}

void MseVideoSinkFeed::needData(GstAppSrc * /*src*/, guint /*length*/, gpointer user)
{
    static_cast<MseVideoSinkFeed *>(user)->pushNextFrame();
}

void MseVideoSinkFeed::enoughData(GstAppSrc * /*src*/, gpointer /*user*/) {}

void MseVideoSinkFeed::pushNextFrame()
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

void MseVideoSinkFeed::signalEos()
{
    if (!m_eosSignalled && m_appsrc != nullptr)
    {
        m_eosSignalled = true;
        gst_app_src_end_of_stream(GST_APP_SRC(m_appsrc));
    }
}

bool MseVideoSinkFeed::reportVideoStream()
{
    if (m_sinkPad == nullptr)
        return false;
    GstStreamCollection *collection = gst_stream_collection_new("rialto-conformance-video");
    GstStream *stream = gst_stream_new("video-0", nullptr, GST_STREAM_TYPE_VIDEO, GST_STREAM_FLAG_NONE);
    gst_stream_collection_add_stream(collection, stream); // transfer-full of stream
    GstEvent *event = gst_event_new_stream_collection(collection);
    gst_object_unref(collection);
    return gst_pad_send_event(m_sinkPad, event);
}

bool MseVideoSinkFeed::startPlaying(std::chrono::milliseconds timeout)
{
    if (!isValid())
        return false;

    const auto deadline = std::chrono::steady_clock::now() + timeout;

    // Request PLAYING; the sink creates its media-player client, connects to the
    // server, loads the MSE session, and prerolls once fed. Returns ASYNC on the
    // software render path.
    GstStateChangeReturn ret = gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE)
        return false;

    // Report one video stream so the sink can send allSourcesAttached. The
    // STREAM_COLLECTION handler only succeeds once the media-player client exists,
    // so retrying until it succeeds is also the client-ready signal. The AVC CAPS
    // the appsrc pushes attaches the source; whichever lands last triggers
    // allSourcesAttached.
    bool reported = false;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (reportVideoStream())
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
    const auto remainingNs = std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - now).count();

    GstState state = GST_STATE_NULL;
    GstState pending = GST_STATE_NULL;
    ret = gst_element_get_state(m_pipeline, &state, &pending, static_cast<GstClockTime>(remainingNs));
    return ret == GST_STATE_CHANGE_SUCCESS && state == GST_STATE_PLAYING;
}

} // namespace rialto::conformance
