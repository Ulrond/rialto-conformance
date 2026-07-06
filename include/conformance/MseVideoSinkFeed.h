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

#ifndef RIALTO_CONFORMANCE_MSE_VIDEO_SINK_FEED_H_
#define RIALTO_CONFORMANCE_MSE_VIDEO_SINK_FEED_H_

/**
 * @file MseVideoSinkFeed.h
 *
 * Video counterpart to MseSinkFeed: a real `appsrc ! rialtomsevideosink` GStreamer
 * pipeline that drives a synthesised H.264 stream through the Rialto MSE video sink
 * to the live RialtoServer the gate provides.
 *
 * Its purpose is the incoming-CAPS-field parse proof of RC-CORE-MSECAPS-006. The
 * appsrc advertises the H.264 stream in **AVC form** — `stream-format=avc`,
 * `alignment=au`, with SPS/PPS out-of-band in `codec_data`. When that CAPS event
 * reaches the sink, the sink's `createMediaSource` must parse those fields (see
 * GStreamerMSEUtils get_codec_data/get_segment_alignment/get_stream_format) and
 * carry them into the Rialto video source it attaches; the server's decoder cannot
 * configure an AVC stream without the `codec_data`, so reaching PLAYING is the
 * field-sensitive evidence that the parse happened and was applied. A byte-stream
 * H.264 stream, which carries SPS/PPS in-band, could not distinguish this.
 *
 * The elementary stream is the same in-process synthesised H.264 the native harness
 * uses (MediaFeed.h::generateH264AvcStream), so a case stays hermetic and needs no
 * network asset. As with the audio feed, the frames are replayed with a monotonic
 * PTS so the sink pad stays live (never EOS) until a case asks otherwise.
 */

#include "MediaFeed.h"

#include <chrono>
#include <cstdint>

#include <gst/app/gstappsrc.h>
#include <gst/gst.h>

namespace rialto::conformance
{
/**
 * Drives a real `appsrc ! rialtomsevideosink` pipeline feeding a synthesised
 * AVC-form H.264 stream through the Rialto MSE video sink to a live RialtoServer.
 *
 * Lifecycle mirrors MseSinkFeed: construct (builds + links the pipeline with the
 * AVC caps that carry codec_data), startPlaying() (attach the video source via
 * CAPS, report one video stream so the sink sends allSourcesAttached, feed the
 * frames, reach PLAYING). Destruction tears the pipeline to NULL.
 */
class MseVideoSinkFeed
{
public:
    /// Build the pipeline for @p stream. Check isValid() before use; the stream
    /// must be isComplete() (frames + codec_data) or the AVC caps cannot be built.
    explicit MseVideoSinkFeed(const H264AvcElementaryStream &stream);
    ~MseVideoSinkFeed();

    MseVideoSinkFeed(const MseVideoSinkFeed &) = delete;
    MseVideoSinkFeed &operator=(const MseVideoSinkFeed &) = delete;

    /// True once the pipeline elements were created and linked.
    bool isValid() const { return m_pipeline != nullptr && m_appsrc != nullptr && m_sink != nullptr; }

    /// The rialtomsevideosink element (transfer-none, owned by the harness).
    GstElement *sink() const { return m_sink; }

    /**
     * Drive the pipeline to PLAYING against the live server: request PLAYING
     * (async), report a single video stream to the sink (so it can send
     * allSourcesAttached — the report only succeeds once the sink's media-player
     * client exists, which the harness uses as the client-ready signal), let the
     * AVC CAPS attach the video source, and feed the frames until the server
     * prerolls and reaches PLAYING.
     *
     * @param[in] timeout  upper bound on reaching PLAYING on the headless path.
     * @retval true if the pipeline reached PLAYING within @p timeout.
     */
    bool startPlaying(std::chrono::milliseconds timeout);

    /// Signal end-of-stream on the appsrc so the stream drains through the sink.
    void signalEos();

private:
    // appsrc need-data: push the next encoded frame, replaying the frame list with
    // an increasing PTS so the stream never ends (the sink pad stays live).
    // Runs on appsrc's streaming thread.
    static void needData(GstAppSrc *src, guint length, gpointer user);
    static void enoughData(GstAppSrc *src, gpointer user);
    void pushNextFrame();

    // Report a single video stream to the sink via GST_EVENT_STREAM_COLLECTION.
    // @retval true if the sink accepted it (its media-player client exists).
    bool reportVideoStream();

    H264AvcElementaryStream m_stream;
    size_t m_nextFrame = 0;
    size_t m_loopCount = 0;         ///< times the frame list has been replayed
    int64_t m_streamDurationNs = 0; ///< PTS offset added per replay loop
    bool m_eosSignalled = false;

    GstElement *m_pipeline = nullptr;
    GstElement *m_appsrc = nullptr;
    GstElement *m_sink = nullptr;
    GstPad *m_sinkPad = nullptr; ///< the sink's static "sink" pad (owned)
};

} // namespace rialto::conformance

#endif // RIALTO_CONFORMANCE_MSE_VIDEO_SINK_FEED_H_
