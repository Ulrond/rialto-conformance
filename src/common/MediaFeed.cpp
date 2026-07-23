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
 * @file MediaFeed.cpp
 *
 * Implementation of the Surface B data-path feed harness (see MediaFeed.h): the
 * in-process AAC elementary-stream synthesiser and the active feeding client.
 */

#include "conformance/MediaFeed.h"

#include <gst/app/gstappsink.h>
#include <gst/gst.h>

#include <memory>
#include <utility>

using namespace firebolt::rialto;

namespace rialto::conformance
{
namespace
{
// The synthesised stream's audio format. A stereo 48 kHz tone gives real AAC the
// server decoder consumes; the values are echoed onto the Rialto audio source so
// the server builds matching caps.
constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kChannels = 2;

// AAC-LC codes 1024 PCM samples per access unit. aacparse's ADTS output does not
// carry a per-buffer duration, so the feed stamps one derived from the sample
// count, and a monotonic PTS when the buffer's own PTS is absent.
constexpr int64_t kAacSamplesPerFrame = 1024;
} // namespace

int64_t AacElementaryStream::totalDurationNs() const
{
    int64_t total = 0;
    for (const auto &frame : frames)
    {
        if (frame.duration > 0)
            total += frame.duration;
    }
    return total;
}

AacElementaryStream generateAacAdtsStream(int numFrames)
{
    AacElementaryStream stream;
    stream.sampleRate = kSampleRate;
    stream.channels = kChannels;

    if (!gst_is_initialized())
        gst_init(nullptr, nullptr);

    // Real encode chain: a bounded test tone -> AAC -> ADTS access units. avenc_aac
    // (+ aacparse) come from the software image's libav/ugly plugins.
    gchar *desc = g_strdup_printf("audiotestsrc num-buffers=%d ! audio/x-raw,rate=%u,channels=%u ! audioconvert ! "
                                  "avenc_aac ! aacparse ! audio/mpeg,mpegversion=4,stream-format=adts ! "
                                  "appsink name=sink sync=false",
                                  numFrames, kSampleRate, kChannels);
    GError *error = nullptr;
    GstElement *pipeline = gst_parse_launch(desc, &error);
    g_free(desc);
    if (error != nullptr)
    {
        g_error_free(error);
    }
    if (pipeline == nullptr)
    {
        return stream; // encode elements unavailable; caller sees an empty stream
    }

    GstElement *sinkElement = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
    GstAppSink *appsink = GST_APP_SINK(sinkElement);

    // Per-frame duration derived from the codec's fixed sample count, used both as
    // the segment duration and to synthesise a monotonic PTS when one is absent.
    const int64_t frameDurationNs = kAacSamplesPerFrame * GST_SECOND / static_cast<int64_t>(kSampleRate);

    if (appsink != nullptr && gst_element_set_state(pipeline, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE)
    {
        int64_t frameIndex = 0;
        while (true)
        {
            GstSample *sample = gst_app_sink_pull_sample(appsink);
            if (sample == nullptr)
                break; // EOS or error

            GstBuffer *buffer = gst_sample_get_buffer(sample);
            GstMapInfo map;
            if (buffer != nullptr && gst_buffer_map(buffer, &map, GST_MAP_READ))
            {
                EncodedFrame frame;
                frame.data.assign(map.data, map.data + map.size);
                frame.timeStamp = GST_BUFFER_PTS_IS_VALID(buffer) ? static_cast<int64_t>(GST_BUFFER_PTS(buffer))
                                                                  : frameIndex * frameDurationNs;
                frame.duration = GST_BUFFER_DURATION_IS_VALID(buffer)
                                     ? static_cast<int64_t>(GST_BUFFER_DURATION(buffer))
                                     : frameDurationNs;
                gst_buffer_unmap(buffer, &map);
                stream.frames.push_back(std::move(frame));
                ++frameIndex;
            }
            gst_sample_unref(sample);
        }
    }

    gst_element_set_state(pipeline, GST_STATE_NULL);
    if (sinkElement != nullptr)
        gst_object_unref(sinkElement);
    gst_object_unref(pipeline);
    return stream;
}

namespace
{
// The synthesised video geometry. A small 320x240 test pattern decodes cheaply on
// the headless software path; the values are echoed onto the Rialto video source.
constexpr uint32_t kVideoWidth = 320;
constexpr uint32_t kVideoHeight = 240;
constexpr int kVideoFrameRate = 25; // frames/second, for the derived PTS/duration

// Extract the avcC codec_data buffer from a sample's negotiated caps, if present.
// AVC-form h264parse output carries SPS/PPS here rather than in-band.
std::vector<uint8_t> extractCodecData(GstSample *sample)
{
    std::vector<uint8_t> codecData;
    GstCaps *caps = gst_sample_get_caps(sample);
    if (caps == nullptr || gst_caps_get_size(caps) == 0)
        return codecData;
    const GstStructure *s = gst_caps_get_structure(caps, 0);
    const GValue *value = gst_structure_get_value(s, "codec_data");
    if (value == nullptr)
        return codecData;
    GstBuffer *buf = gst_value_get_buffer(value);
    GstMapInfo map;
    if (buf != nullptr && gst_buffer_map(buf, &map, GST_MAP_READ))
    {
        codecData.assign(map.data, map.data + map.size);
        gst_buffer_unmap(buf, &map);
    }
    return codecData;
}
} // namespace

int64_t H264AvcElementaryStream::totalDurationNs() const
{
    int64_t total = 0;
    for (const auto &frame : frames)
    {
        if (frame.duration > 0)
            total += frame.duration;
    }
    return total;
}

H264AvcElementaryStream generateH264AvcStream(int numFrames)
{
    H264AvcElementaryStream stream;
    stream.width = kVideoWidth;
    stream.height = kVideoHeight;

    if (!gst_is_initialized())
        gst_init(nullptr, nullptr);

    // Real encode chain: a bounded test pattern -> H.264 -> AVC-form access units.
    // Forcing stream-format=avc,alignment=au on the h264parse output moves SPS/PPS
    // out-of-band into codec_data (the whole point of the parse proof). x264enc +
    // h264parse come from the software image's ugly/bad plugins.
    gchar *desc = g_strdup_printf("videotestsrc num-buffers=%d ! "
                                  "video/x-raw,width=%u,height=%u,framerate=%d/1 ! "
                                  "x264enc ! h264parse ! video/x-h264,stream-format=avc,alignment=au ! "
                                  "appsink name=sink sync=false",
                                  numFrames, kVideoWidth, kVideoHeight, kVideoFrameRate);
    GError *error = nullptr;
    GstElement *pipeline = gst_parse_launch(desc, &error);
    g_free(desc);
    if (error != nullptr)
    {
        g_error_free(error);
    }
    if (pipeline == nullptr)
    {
        return stream; // encode elements unavailable; caller sees an incomplete stream
    }

    GstElement *sinkElement = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
    GstAppSink *appsink = GST_APP_SINK(sinkElement);

    // Per-frame duration derived from the constant frame rate, used both as the
    // segment duration and to synthesise a monotonic PTS when one is absent.
    const int64_t frameDurationNs = GST_SECOND / static_cast<int64_t>(kVideoFrameRate);

    if (appsink != nullptr && gst_element_set_state(pipeline, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE)
    {
        int64_t frameIndex = 0;
        while (true)
        {
            GstSample *sample = gst_app_sink_pull_sample(appsink);
            if (sample == nullptr)
                break; // EOS or error

            // codec_data is negotiated on the caps; capture it from the first sample
            // that carries it (it is stable for the stream's lifetime).
            if (stream.codecData.empty())
                stream.codecData = extractCodecData(sample);

            GstBuffer *buffer = gst_sample_get_buffer(sample);
            GstMapInfo map;
            if (buffer != nullptr && gst_buffer_map(buffer, &map, GST_MAP_READ))
            {
                EncodedFrame frame;
                frame.data.assign(map.data, map.data + map.size);
                frame.timeStamp = GST_BUFFER_PTS_IS_VALID(buffer) ? static_cast<int64_t>(GST_BUFFER_PTS(buffer))
                                                                  : frameIndex * frameDurationNs;
                frame.duration = GST_BUFFER_DURATION_IS_VALID(buffer)
                                     ? static_cast<int64_t>(GST_BUFFER_DURATION(buffer))
                                     : frameDurationNs;
                gst_buffer_unmap(buffer, &map);
                stream.frames.push_back(std::move(frame));
                ++frameIndex;
            }
            gst_sample_unref(sample);
        }
    }

    gst_element_set_state(pipeline, GST_STATE_NULL);
    if (sinkElement != nullptr)
        gst_object_unref(sinkElement);
    gst_object_unref(pipeline);
    return stream;
}

void FeedingMediaPipelineClient::setPipeline(IMediaPipeline *pipeline)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pipeline = pipeline;
}

void FeedingMediaPipelineClient::addAudioSource(int32_t sourceId, const AacElementaryStream &stream)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_feeds[sourceId] = SourceFeed{stream, 0};
}

void FeedingMediaPipelineClient::rewindSource(int32_t sourceId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_feeds.find(sourceId);
    if (it != m_feeds.end())
    {
        it->second.nextFrame = 0;
    }
}

bool FeedingMediaPipelineClient::waitForPlaybackState(PlaybackState state, std::chrono::milliseconds timeout)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    return m_cv.wait_for(lock, timeout, [&] { return m_statesSeen.count(state) != 0; });
}

bool FeedingMediaPipelineClient::sawPlaybackState(PlaybackState state)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_statesSeen.count(state) != 0;
}

void FeedingMediaPipelineClient::notifyNeedMediaData(int32_t sourceId, size_t frameCount, uint32_t needDataRequestId,
                                                     const std::shared_ptr<MediaPlayerShmInfo> &shmInfo)
{
    // Build the batch of segments to offer this round, without advancing the feed
    // cursor: the cursor only moves for segments the server actually accepts.
    std::vector<std::unique_ptr<IMediaPipeline::MediaSegment>> batch;
    uint32_t sampleRate = 0;
    uint32_t channels = 0;
    bool starve = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        // Protocol observation (RC-CORE-DATA-001/002): record the request shape
        // and flag a request that overlaps an unanswered one for the same source.
        m_needDataLog.push_back(NeedDataRecord{sourceId, frameCount, needDataRequestId, shmInfo != nullptr});
        if (m_pendingNeedData.count(sourceId) != 0)
        {
            m_overlappingNeedData = true;
        }
        m_pendingNeedData.insert(sourceId);
        starve = m_starve;
        if (m_pipeline == nullptr)
            return;
        auto it = m_feeds.find(sourceId);
        if (it == m_feeds.end())
            return;
        const SourceFeed &feed = it->second;
        sampleRate = feed.stream.sampleRate;
        channels = feed.stream.channels;
        for (size_t idx = feed.nextFrame; idx < feed.stream.frames.size() && batch.size() < frameCount; ++idx)
        {
            const EncodedFrame &frame = feed.stream.frames[idx];
            auto segment = std::make_unique<IMediaPipeline::MediaSegmentAudio>(
                sourceId, frame.timeStamp, frame.duration, static_cast<int32_t>(sampleRate),
                static_cast<int32_t>(channels));
            segment->setData(static_cast<uint32_t>(frame.data.size()), frame.data.data());
            batch.push_back(std::move(segment));
        }
    }

    // Starve mode (data-protocol cases): answer the request with no segments so
    // the server's queue drains and it raises buffer underflow.
    if (starve)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_pendingNeedData.erase(sourceId);
        }
        m_pipeline->haveData(MediaSourceStatus::OK, needDataRequestId);
        return;
    }

    // Offer the segments outside the lock (addSegment/haveData are server IPC
    // calls). Stop at the first non-OK status; only accepted segments advance.
    size_t accepted = 0;
    for (auto &segment : batch)
    {
        if (m_pipeline->addSegment(needDataRequestId, segment) != AddSegmentStatus::OK)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_allSegmentsAccepted = false; // RC-CORE-DATA-003 observation
            break;
        }
        ++accepted;
    }

    bool atEndOfStream = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_feeds.find(sourceId);
        if (it != m_feeds.end())
        {
            it->second.nextFrame += accepted;
            atEndOfStream = it->second.nextFrame >= it->second.stream.frames.size();
        }
        m_pendingNeedData.erase(sourceId); // answered below (RC-CORE-DATA-002)
    }

    m_pipeline->haveData(atEndOfStream ? MediaSourceStatus::EOS : MediaSourceStatus::OK, needDataRequestId);
}

void FeedingMediaPipelineClient::notifyPlaybackState(PlaybackState state)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_statesSeen.insert(state);
    }
    m_cv.notify_all();
}

void FeedingMediaPipelineClient::notifyDuration(int64_t) {}
void FeedingMediaPipelineClient::notifyPosition(int64_t position)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    ++m_positionUpdates;
    m_lastPosition = position;
}
void FeedingMediaPipelineClient::notifyNativeSize(uint32_t, uint32_t, double) {}
void FeedingMediaPipelineClient::notifyNetworkState(NetworkState state)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_networkStatesSeen.insert(state);
}
void FeedingMediaPipelineClient::notifyVideoData(bool) {}
void FeedingMediaPipelineClient::notifyAudioData(bool) {}
void FeedingMediaPipelineClient::notifyCancelNeedMediaData(int32_t) {}
void FeedingMediaPipelineClient::notifyQos(int32_t, const QosInfo &qosInfo)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    ++m_qosCount;
    m_lastQos = qosInfo;
}
void FeedingMediaPipelineClient::notifyBufferUnderflow(int32_t sourceId)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_underflowSources.insert(sourceId);
    }
    m_cv.notify_all();
}
void FeedingMediaPipelineClient::notifyFirstFrameReceived(int32_t sourceId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_firstFrameSources.insert(sourceId);
}
void FeedingMediaPipelineClient::notifyPlaybackError(int32_t, PlaybackError) {}
void FeedingMediaPipelineClient::notifySourceFlushed(int32_t sourceId)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_flushedSources.insert(sourceId);
    }
    m_cv.notify_all();
}

bool FeedingMediaPipelineClient::waitForSourceFlushed(int32_t sourceId, std::chrono::milliseconds timeout)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    return m_cv.wait_for(lock, timeout, [&] { return m_flushedSources.count(sourceId) != 0; });
}
void FeedingMediaPipelineClient::notifyPlaybackInfo(const PlaybackInfo &) {}

std::vector<FeedingMediaPipelineClient::NeedDataRecord> FeedingMediaPipelineClient::needDataLog()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_needDataLog;
}

bool FeedingMediaPipelineClient::sawOverlappingNeedData()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_overlappingNeedData;
}

bool FeedingMediaPipelineClient::allSegmentsAccepted()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_allSegmentsAccepted;
}

std::set<NetworkState> FeedingMediaPipelineClient::networkStatesSeen()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_networkStatesSeen;
}

void FeedingMediaPipelineClient::setStarve(bool starve)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_starve = starve;
}

bool FeedingMediaPipelineClient::waitForBufferUnderflow(int32_t sourceId, std::chrono::milliseconds timeout)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    return m_cv.wait_for(lock, timeout, [&] { return m_underflowSources.count(sourceId) != 0; });
}

bool FeedingMediaPipelineClient::sawFirstFrame(int32_t sourceId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_firstFrameSources.count(sourceId) != 0;
}

size_t FeedingMediaPipelineClient::positionUpdateCount()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_positionUpdates;
}

int64_t FeedingMediaPipelineClient::lastPosition()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lastPosition;
}

size_t FeedingMediaPipelineClient::qosCount()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_qosCount;
}

firebolt::rialto::QosInfo FeedingMediaPipelineClient::lastQos()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lastQos;
}

} // namespace rialto::conformance
