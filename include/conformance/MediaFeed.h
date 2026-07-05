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

#ifndef RIALTO_CONFORMANCE_MEDIA_FEED_H_
#define RIALTO_CONFORMANCE_MEDIA_FEED_H_

/**
 * @file MediaFeed.h
 *
 * Data-path feed harness for the Surface B (native IMediaPipeline) conformance
 * cases (§7.1). These cases must drive *real* elementary-stream buffers through
 * a live RialtoServer — attach a source, answer the server's need-data requests
 * with decodable samples, and reach PLAYING — rather than merely introspecting
 * synchronous getters. This header provides the two pieces every such case
 * needs:
 *
 *   1. A real elementary stream to feed. Until the curated corpus (issue #22)
 *      lands, streams are synthesised in-process with GStreamer's own encoders
 *      (the software image ships libav/ugly), so a case is hermetic and needs no
 *      network asset. The bytes are genuine AAC the server's decoder consumes.
 *
 *   2. An active IMediaPipelineClient that answers notifyNeedMediaData by adding
 *      the stream's segments and calling haveData, and records the playback-state
 *      notifications so a case can wait for PLAYING / EOS. This is the exact
 *      contract an external media app implements; the suite implements it the
 *      same way.
 */

#include "IMediaPipeline.h"
#include "IMediaPipelineClient.h"
#include "MediaCommon.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <vector>

namespace rialto::conformance
{
/**
 * A single encoded access unit of an elementary stream, with the presentation
 * timestamp and duration the feed stamps onto the Rialto media segment.
 */
struct EncodedFrame
{
    std::vector<uint8_t> data; ///< the encoded bytes of one access unit
    int64_t timeStamp;         ///< presentation timestamp, nanoseconds
    int64_t duration;          ///< sample duration, nanoseconds
};

/**
 * A synthesised AAC elementary stream: ADTS-framed access units plus the sample
 * rate / channel count a case declares on the Rialto audio source. ADTS is
 * self-describing, so the server's aacparse configures the decoder from the
 * bytes.
 */
struct AacElementaryStream
{
    std::vector<EncodedFrame> frames;
    uint32_t sampleRate = 0;
    uint32_t channels = 0;

    /// Total presentation duration of the stream in nanoseconds.
    int64_t totalDurationNs() const;
};

/**
 * @brief Synthesise a short clear AAC (ADTS) elementary stream in-process.
 *
 * Encodes a test tone through GStreamer's own audiotestsrc -> avenc_aac ->
 * aacparse chain and captures the ADTS access units. The bytes are real AAC the
 * RialtoServer decoder (avdec_aac) plays; nothing is mocked.
 *
 * @param[in] numFrames  approximate number of AAC access units to produce.
 * @retval the stream; frames is empty if the encode elements are unavailable.
 */
AacElementaryStream generateAacAdtsStream(int numFrames = 50);

/**
 * An IMediaPipelineClient that feeds a resolved elementary stream to a live
 * RialtoServer and records the playback-state notifications.
 *
 * Usage: create the client, create the pipeline bound to it, attach a source,
 * register the source's stream with addAudioSource(id, stream), then
 * allSourcesAttached() + play(). The server's notifyNeedMediaData callbacks
 * (delivered on the client IPC thread) are answered here by adding segments and
 * calling haveData; when a source's frames are exhausted the feed reports EOS.
 *
 * The pipeline must outlive the client's feeding activity: destroy the pipeline
 * (which stops need-data callbacks) before releasing the client.
 */
class FeedingMediaPipelineClient : public firebolt::rialto::IMediaPipelineClient
{
public:
    FeedingMediaPipelineClient() = default;

    /// Bind the pipeline the feed calls addSegment/haveData on. Must be set
    /// before allSourcesAttached()/play() so an early need-data can be answered.
    void setPipeline(firebolt::rialto::IMediaPipeline *pipeline);

    /// Register the stream to feed for a source id returned by attachSource().
    void addAudioSource(int32_t sourceId, const AacElementaryStream &stream);

    /// Block until @p state has been notified, up to @p timeout.
    /// @retval true if the state was observed within the timeout.
    bool waitForPlaybackState(firebolt::rialto::PlaybackState state, std::chrono::milliseconds timeout);

    /// True if @p state has been notified at any point.
    bool sawPlaybackState(firebolt::rialto::PlaybackState state);

    // --- IMediaPipelineClient ------------------------------------------------
    void notifyDuration(int64_t duration) override;
    void notifyPosition(int64_t position) override;
    void notifyNativeSize(uint32_t width, uint32_t height, double aspect) override;
    void notifyNetworkState(firebolt::rialto::NetworkState state) override;
    void notifyPlaybackState(firebolt::rialto::PlaybackState state) override;
    void notifyVideoData(bool hasData) override;
    void notifyAudioData(bool hasData) override;
    void notifyNeedMediaData(int32_t sourceId, size_t frameCount, uint32_t needDataRequestId,
                             const std::shared_ptr<firebolt::rialto::MediaPlayerShmInfo> &shmInfo) override;
    void notifyCancelNeedMediaData(int32_t sourceId) override;
    void notifyQos(int32_t sourceId, const firebolt::rialto::QosInfo &qosInfo) override;
    void notifyBufferUnderflow(int32_t sourceId) override;
    void notifyFirstFrameReceived(int32_t sourceId) override;
    void notifyPlaybackError(int32_t sourceId, firebolt::rialto::PlaybackError error) override;
    void notifySourceFlushed(int32_t sourceId) override;
    void notifyPlaybackInfo(const firebolt::rialto::PlaybackInfo &playbackInfo) override;

private:
    /// Per-source feed cursor over the registered stream.
    struct SourceFeed
    {
        AacElementaryStream stream;
        size_t nextFrame = 0;
    };

    std::mutex m_mutex;
    std::condition_variable m_cv;
    firebolt::rialto::IMediaPipeline *m_pipeline = nullptr;
    std::map<int32_t, SourceFeed> m_feeds;
    std::set<firebolt::rialto::PlaybackState> m_statesSeen;
};

} // namespace rialto::conformance

#endif // RIALTO_CONFORMANCE_MEDIA_FEED_H_
