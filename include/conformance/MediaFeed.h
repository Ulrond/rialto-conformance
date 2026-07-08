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
 * A synthesised H.264 elementary stream in AVC form: length-prefixed access units
 * plus the out-of-band @c codec_data (the @c avcC record carrying SPS/PPS) and the
 * frame geometry a case declares on the Rialto video source. AVC form is
 * deliberate — its access units carry no in-band SPS/PPS, so the stream is
 * undecodable unless @c codec_data reaches the decoder. That makes reaching PLAYING
 * a field-sensitive proof that the sink parsed @c codec_data (and the
 * @c stream-format / @c alignment that select AVC form) out of the incoming CAPS
 * event (RC-CORE-MSECAPS-006).
 */
struct H264AvcElementaryStream
{
    std::vector<EncodedFrame> frames;
    std::vector<uint8_t> codecData; ///< the avcC record from the negotiated caps
    uint32_t width = 0;
    uint32_t height = 0;

    /// Total presentation duration of the stream in nanoseconds.
    int64_t totalDurationNs() const;

    /// True once frames and the codec_data (SPS/PPS) were both captured — an AVC
    /// stream missing either is unusable for the parse proof.
    bool isComplete() const { return !frames.empty() && !codecData.empty(); }
};

/**
 * @brief Synthesise a short clear H.264 elementary stream in AVC form in-process.
 *
 * Encodes a test pattern through GStreamer's own videotestsrc -> x264enc ->
 * h264parse chain, forcing @c stream-format=avc,alignment=au on the parser output
 * so SPS/PPS move out-of-band into @c codec_data. Captures the AVC access units and
 * that @c codec_data. The bytes are real H.264 the RialtoServer decoder
 * (avdec_h264) plays; nothing is mocked.
 *
 * @param[in] numFrames  approximate number of encoded frames to produce.
 * @retval the stream; isComplete() is false if the encode elements are unavailable.
 */
H264AvcElementaryStream generateH264AvcStream(int numFrames = 30);

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

    /// Rewind the feed cursor for @p sourceId to the first frame. A seek makes
    /// the server flush and re-request data from the seek target; an MSE app
    /// re-appends from there, which for a seek-to-start this replay reproduces.
    void rewindSource(int32_t sourceId);

    /// Block until @p state has been notified, up to @p timeout.
    /// @retval true if the state was observed within the timeout.
    bool waitForPlaybackState(firebolt::rialto::PlaybackState state, std::chrono::milliseconds timeout);

    /// True if @p state has been notified at any point.
    bool sawPlaybackState(firebolt::rialto::PlaybackState state);

    /// Block until notifySourceFlushed(@p sourceId) has been received, up to
    /// @p timeout. @retval true if the flush notification was observed.
    bool waitForSourceFlushed(int32_t sourceId, std::chrono::milliseconds timeout);

    // --- Data-protocol observation (RC-CORE-DATA-*) --------------------------
    // The feed records the shape of every need-data interaction so a case can
    // assert the documented protocol after a run, without altering the feeding.

    /// One observed notifyNeedMediaData request.
    struct NeedDataRecord
    {
        int32_t sourceId;
        size_t frameCount;
        uint32_t requestId;
        bool hadShmInfo;
    };

    /// The need-data requests observed so far (copy).
    std::vector<NeedDataRecord> needDataLog();

    /// True if a need-data ever arrived for a source whose previous request had
    /// not yet been answered with haveData (the DATA-002 violation).
    bool sawOverlappingNeedData();

    /// True if every addSegment made by the feed returned OK.
    bool allSegmentsAccepted();

    /// The set of network states notified so far (copy).
    std::set<firebolt::rialto::NetworkState> networkStatesSeen();

    /// Starve mode: when enabled, need-data requests are answered with
    /// haveData(OK) and no segments, so the server's queue drains (provokes
    /// buffer underflow).
    void setStarve(bool starve);

    /// Block until notifyBufferUnderflow(@p sourceId), up to @p timeout.
    bool waitForBufferUnderflow(int32_t sourceId, std::chrono::milliseconds timeout);

    /// True if notifyFirstFrameReceived(@p sourceId) has been observed.
    bool sawFirstFrame(int32_t sourceId);

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
    std::set<int32_t> m_flushedSources;

    // Data-protocol observation.
    std::vector<NeedDataRecord> m_needDataLog;
    std::set<int32_t> m_pendingNeedData; ///< sources with an unanswered request
    bool m_overlappingNeedData = false;
    bool m_allSegmentsAccepted = true;
    std::set<firebolt::rialto::NetworkState> m_networkStatesSeen;
    bool m_starve = false;
    std::set<int32_t> m_underflowSources;
    std::set<int32_t> m_firstFrameSources;
};

} // namespace rialto::conformance

#endif // RIALTO_CONFORMANCE_MEDIA_FEED_H_
