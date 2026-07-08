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
 * @file PipelineAuxDataPathTests.cpp
 *
 * L4 — end-to-end data-path cases for the auxiliary IMediaPipeline surface that
 * requires a realized backend pipeline: volume, per-source mute, video window,
 * flush + notifySourceFlushed, per-source position, audio-gap processing, the
 * buffering controls, and renderFrame. Each drives a real synthesised AAC source
 * through a live RialtoServer (MediaFeed) to PLAYING first — on a fresh pipeline
 * these calls have nothing to apply to (the buffering getters, for instance,
 * return false unrealized; see the PIPE-029/030 history in coverage/matrix.yaml).
 *
 * Coverage trace: coverage/rc-core-catalog.yaml / matrix.yaml —
 * RC-CORE-PIPE-015 (setVideoWindow), -017/-018 (set/getVolume),
 * -019 (set/getMute per source), -025 (flush + notifySourceFlushed),
 * -026 (setSourcePosition), -028 (processAudioGap), -029 (buffering limit),
 * -030 (use-buffering flag), -032 (renderFrame).
 */

#include <ut.h>
#include <ut_log.h>

#include "conformance/MediaFeed.h"
#include "conformance/Surfaces.h"
#include "conformance/TierGate.h"

#include "IMediaPipeline.h"
#include "MediaCommon.h"

#include <chrono>
#include <cmath>
#include <memory>
#include <thread>

using namespace firebolt::rialto;
using rialto::conformance::AacElementaryStream;
using rialto::conformance::FeedingMediaPipelineClient;
using rialto::conformance::generateAacAdtsStream;
using rialto::conformance::NativeClientSurface;

namespace
{
constexpr uint32_t kMaxWidth = 1920;
constexpr uint32_t kMaxHeight = 1080;

// Enough AAC for the pipeline to reach and hold PLAYING while the calls under
// test are made (~4s at 48 kHz).
constexpr int kAuxFrames = 200;

constexpr std::chrono::milliseconds kPlayingTimeout{15000};
constexpr std::chrono::milliseconds kNotifyTimeout{5000};

// The property setters (volume, mute, use-buffering) enqueue their write to the
// server's worker thread and return; application is asynchronous. A read-back is
// therefore polled until the value applies, up to this deadline.
constexpr std::chrono::milliseconds kApplyDeadline{5000};
constexpr std::chrono::milliseconds kApplyPoll{50};

/// Poll @p read until it returns true and @p applied holds, up to kApplyDeadline.
/// @retval true when the read-back reflected the written value in time.
template <typename ReadFn>
bool pollUntilApplied(ReadFn read)
{
    const auto deadline = std::chrono::steady_clock::now() + kApplyDeadline;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (read())
            return true;
        std::this_thread::sleep_for(kApplyPoll);
    }
    return read();
}

/**
 * Bring up the full data path: create a pipeline bound to a fresh feed client,
 * load MSE, attach the AAC source, allSourcesAttached, play, and wait for
 * PLAYING. Returns the realized pipeline (nullptr on any failure) and writes the
 * attached source id.
 */
std::unique_ptr<IMediaPipeline> driveToPlaying(const std::shared_ptr<FeedingMediaPipelineClient> &client,
                                               const AacElementaryStream &stream, int32_t &sourceIdOut)
{
    auto factory = IMediaPipelineFactory::createFactory();
    if (!factory)
        return nullptr;

    VideoRequirements requirements{kMaxWidth, kMaxHeight};
    std::unique_ptr<IMediaPipeline> pipeline = factory->createMediaPipeline(client, requirements);
    if (!pipeline)
        return nullptr;
    client->setPipeline(pipeline.get());

    if (!pipeline->load(MediaType::MSE, "", "mse://1", false))
        return nullptr;

    AudioConfig audioConfig;
    audioConfig.numberOfChannels = stream.channels;
    audioConfig.sampleRate = stream.sampleRate;
    std::unique_ptr<IMediaPipeline::MediaSource> source =
        std::make_unique<IMediaPipeline::MediaSourceAudio>("audio/mp4", false, audioConfig);
    if (!pipeline->attachSource(source))
        return nullptr;
    sourceIdOut = source->getId();
    client->addAudioSource(sourceIdOut, stream);

    if (!pipeline->allSourcesAttached())
        return nullptr;
    bool async = false;
    if (!pipeline->play(async))
        return nullptr;
    if (!client->waitForPlaybackState(PlaybackState::PLAYING, kPlayingTimeout))
        return nullptr;
    return pipeline;
}

class L4PipelineAuxDataPathTests : public NativeClientSurface
{
};
} // namespace

UT_ADD_TEST_TO_GROUP(L4PipelineAuxDataPathTests, UT_TESTS_L4);

/**
 * RC-CORE-PIPE-017 + RC-CORE-PIPE-018 — setVolume accepts a target in [0.0, 1.0]
 * and returns true; getVolume then returns true and writes the level back. The
 * playbin-backed volume is a real property, so the set round-trips.
 */
UT_ADD_TEST(L4PipelineAuxDataPathTests, VolumeRoundTripsOnRealizedPipeline)
{
    CONFORMANCE_CORE_TEST();

    AacElementaryStream stream = generateAacAdtsStream(kAuxFrames);
    UT_ASSERT_TRUE_FATAL(!stream.frames.empty());
    auto client = std::make_shared<FeedingMediaPipelineClient>();
    int32_t sourceId = -1;
    std::unique_ptr<IMediaPipeline> pipeline = driveToPlaying(client, stream, sourceId);
    UT_ASSERT_NOT_NULL_FATAL(pipeline.get());

    constexpr double kTarget = 0.5;
    UT_ASSERT_TRUE(pipeline->setVolume(kTarget));

    // The write is applied asynchronously on the server's worker thread; poll the
    // synchronous getter until it reflects the target.
    double readBack = -1.0;
    const bool applied = pollUntilApplied(
        [&] { return pipeline->getVolume(readBack) && std::fabs(readBack - kTarget) < 0.01; });
    UT_LOG("[aux] volume set %.2f read %.2f applied=%d", kTarget, readBack, applied);
    UT_ASSERT_TRUE(readBack >= 0.0 && readBack <= 1.0);
    UT_ASSERT_TRUE(applied);

    pipeline->stop();
    pipeline.reset();
}

/**
 * RC-CORE-PIPE-019 — setMute / getMute round-trip the mute flag per source.
 */
UT_ADD_TEST(L4PipelineAuxDataPathTests, MuteRoundTripsPerSource)
{
    CONFORMANCE_CORE_TEST();

    AacElementaryStream stream = generateAacAdtsStream(kAuxFrames);
    UT_ASSERT_TRUE_FATAL(!stream.frames.empty());
    auto client = std::make_shared<FeedingMediaPipelineClient>();
    int32_t sourceId = -1;
    std::unique_ptr<IMediaPipeline> pipeline = driveToPlaying(client, stream, sourceId);
    UT_ASSERT_NOT_NULL_FATAL(pipeline.get());

    // Each write applies asynchronously on the server's worker thread; poll the
    // getter for the new value.
    UT_ASSERT_TRUE(pipeline->setMute(sourceId, true));
    bool muted = false;
    UT_ASSERT_TRUE(pollUntilApplied([&] { return pipeline->getMute(sourceId, muted) && muted; }));

    UT_ASSERT_TRUE(pipeline->setMute(sourceId, false));
    UT_ASSERT_TRUE(pollUntilApplied([&] { return pipeline->getMute(sourceId, muted) && !muted; }));

    pipeline->stop();
    pipeline.reset();
}

/**
 * RC-CORE-PIPE-015 — setVideoWindow accepts pixel x/y/width/height and returns
 * true (the geometry is queued and applied to the video sink when present).
 */
UT_ADD_TEST(L4PipelineAuxDataPathTests, SetVideoWindowAcceptsGeometry)
{
    CONFORMANCE_CORE_TEST();

    AacElementaryStream stream = generateAacAdtsStream(kAuxFrames);
    UT_ASSERT_TRUE_FATAL(!stream.frames.empty());
    auto client = std::make_shared<FeedingMediaPipelineClient>();
    int32_t sourceId = -1;
    std::unique_ptr<IMediaPipeline> pipeline = driveToPlaying(client, stream, sourceId);
    UT_ASSERT_NOT_NULL_FATAL(pipeline.get());

    UT_ASSERT_TRUE(pipeline->setVideoWindow(0, 0, 1280, 720));

    pipeline->stop();
    pipeline.reset();
}

/**
 * RC-CORE-PIPE-025 — flush(sourceId, resetTime, async) returns true and the
 * client receives notifySourceFlushed(sourceId) once the flush completes.
 */
UT_ADD_TEST(L4PipelineAuxDataPathTests, FlushEmitsSourceFlushed)
{
    CONFORMANCE_CORE_TEST();

    AacElementaryStream stream = generateAacAdtsStream(kAuxFrames);
    UT_ASSERT_TRUE_FATAL(!stream.frames.empty());
    auto client = std::make_shared<FeedingMediaPipelineClient>();
    int32_t sourceId = -1;
    std::unique_ptr<IMediaPipeline> pipeline = driveToPlaying(client, stream, sourceId);
    UT_ASSERT_NOT_NULL_FATAL(pipeline.get());

    bool async = false;
    UT_ASSERT_TRUE(pipeline->flush(sourceId, true, async));
    UT_ASSERT_TRUE(client->waitForSourceFlushed(sourceId, kNotifyTimeout));
    UT_LOG("[aux] flush ok (async=%d), notifySourceFlushed received", async);

    pipeline->stop();
    pipeline.reset();
}

/**
 * RC-CORE-PIPE-026 — setSourcePosition sets a per-source start position
 * (appliedRate and stopPosition honoured) and returns true on the realized
 * pipeline.
 */
UT_ADD_TEST(L4PipelineAuxDataPathTests, SetSourcePositionSucceeds)
{
    CONFORMANCE_CORE_TEST();

    AacElementaryStream stream = generateAacAdtsStream(kAuxFrames);
    UT_ASSERT_TRUE_FATAL(!stream.frames.empty());
    auto client = std::make_shared<FeedingMediaPipelineClient>();
    int32_t sourceId = -1;
    std::unique_ptr<IMediaPipeline> pipeline = driveToPlaying(client, stream, sourceId);
    UT_ASSERT_NOT_NULL_FATAL(pipeline.get());

    UT_ASSERT_TRUE(pipeline->setSourcePosition(sourceId, 0, false, 1.0));

    pipeline->stop();
    pipeline.reset();
}

/**
 * RC-CORE-PIPE-028 — processAudioGap handles an audio gap (audioAac flag) and
 * returns true on the realized pipeline.
 */
UT_ADD_TEST(L4PipelineAuxDataPathTests, ProcessAudioGapSucceeds)
{
    CONFORMANCE_CORE_TEST();

    AacElementaryStream stream = generateAacAdtsStream(kAuxFrames);
    UT_ASSERT_TRUE_FATAL(!stream.frames.empty());
    auto client = std::make_shared<FeedingMediaPipelineClient>();
    int32_t sourceId = -1;
    std::unique_ptr<IMediaPipeline> pipeline = driveToPlaying(client, stream, sourceId);
    UT_ASSERT_NOT_NULL_FATAL(pipeline.get());

    // A ~21ms gap at the current position, AAC audio.
    UT_ASSERT_TRUE(pipeline->processAudioGap(0, 21333, 0, true));

    pipeline->stop();
    pipeline.reset();
}

/**
 * RC-CORE-PIPE-029 — setBufferingLimit is accepted (returns true) on the realized
 * pipeline; the limit-buffering-ms value round-trips where the platform's audio
 * decoder exposes the property. It is a vendor-decoder property (absent from the
 * software avdec_aac), so the round-trip conforms-when-supported: when
 * getBufferingLimit answers, the value must be the one set; where the decoder
 * lacks the property, getBufferingLimit answers false (unsupported), which is
 * logged, not failed — the same conform-when-installed pattern as the
 * server-conditional sink properties (MSEPROP-005/008).
 */
UT_ADD_TEST(L4PipelineAuxDataPathTests, BufferingLimitConformsWhenSupported)
{
    CONFORMANCE_CORE_TEST();

    AacElementaryStream stream = generateAacAdtsStream(kAuxFrames);
    UT_ASSERT_TRUE_FATAL(!stream.frames.empty());
    auto client = std::make_shared<FeedingMediaPipelineClient>();
    int32_t sourceId = -1;
    std::unique_ptr<IMediaPipeline> pipeline = driveToPlaying(client, stream, sourceId);
    UT_ASSERT_NOT_NULL_FATAL(pipeline.get());

    constexpr uint32_t kLimitMs = 5000;
    UT_ASSERT_TRUE(pipeline->setBufferingLimit(kLimitMs));

    uint32_t readBack = 0;
    const bool supported =
        pollUntilApplied([&] { return pipeline->getBufferingLimit(readBack) && readBack == kLimitMs; });
    if (supported)
    {
        UT_LOG("[aux] buffering limit round-tripped: %u", readBack);
        UT_ASSERT_EQUAL(readBack, kLimitMs);
    }
    else
    {
        // Unsupported decoder (no limit-buffering-ms property): the getter must
        // consistently answer false rather than a bogus value.
        uint32_t unsupportedRead = 0;
        UT_ASSERT_FALSE(pipeline->getBufferingLimit(unsupportedRead));
        UT_LOG("[aux] buffering limit unsupported by this platform's audio decoder (conform-when-supported)");
    }

    pipeline->stop();
    pipeline.reset();
}

/**
 * RC-CORE-PIPE-030 — setUseBuffering / getUseBuffering round-trip the
 * buffering-messages flag on the realized pipeline.
 */
UT_ADD_TEST(L4PipelineAuxDataPathTests, UseBufferingRoundTrips)
{
    CONFORMANCE_CORE_TEST();

    AacElementaryStream stream = generateAacAdtsStream(kAuxFrames);
    UT_ASSERT_TRUE_FATAL(!stream.frames.empty());
    auto client = std::make_shared<FeedingMediaPipelineClient>();
    int32_t sourceId = -1;
    std::unique_ptr<IMediaPipeline> pipeline = driveToPlaying(client, stream, sourceId);
    UT_ASSERT_NOT_NULL_FATAL(pipeline.get());

    // The write applies asynchronously (queued to the worker thread, applied to
    // the audio decodebin's use-buffering); poll the getter for the new value.
    UT_ASSERT_TRUE(pipeline->setUseBuffering(true));
    bool useBuffering = false;
    UT_ASSERT_TRUE(pollUntilApplied([&] { return pipeline->getUseBuffering(useBuffering) && useBuffering; }));

    pipeline->stop();
    pipeline.reset();
}

/**
 * RC-CORE-PIPE-032 — renderFrame requests rendering of a prerolled frame and
 * returns true once the player is loaded (the request is queued to the pipeline).
 */
UT_ADD_TEST(L4PipelineAuxDataPathTests, RenderFrameSucceedsOnceLoaded)
{
    CONFORMANCE_CORE_TEST();

    AacElementaryStream stream = generateAacAdtsStream(kAuxFrames);
    UT_ASSERT_TRUE_FATAL(!stream.frames.empty());
    auto client = std::make_shared<FeedingMediaPipelineClient>();
    int32_t sourceId = -1;
    std::unique_ptr<IMediaPipeline> pipeline = driveToPlaying(client, stream, sourceId);
    UT_ASSERT_NOT_NULL_FATAL(pipeline.get());

    // Preroll context: move to PAUSED first, then request the frame render.
    UT_ASSERT_TRUE(pipeline->pause());
    UT_ASSERT_TRUE(client->waitForPlaybackState(PlaybackState::PAUSED, kPlayingTimeout));

    UT_ASSERT_TRUE(pipeline->renderFrame());

    pipeline->stop();
    pipeline.reset();
}

/**
 * RC-CORE-PIPE-021 — setLowLatency (default false) returns true: the request is
 * accepted on the realized pipeline. low-latency is a vendor audio-sink property
 * (absent from the software fake sink), and the API exposes no getter, so the
 * accept contract is the assertable clause; application is
 * conform-when-supported on the platform's sink.
 */
UT_ADD_TEST(L4PipelineAuxDataPathTests, SetLowLatencyIsAccepted)
{
    CONFORMANCE_CORE_TEST();

    AacElementaryStream stream = generateAacAdtsStream(kAuxFrames);
    UT_ASSERT_TRUE_FATAL(!stream.frames.empty());
    auto client = std::make_shared<FeedingMediaPipelineClient>();
    int32_t sourceId = -1;
    std::unique_ptr<IMediaPipeline> pipeline = driveToPlaying(client, stream, sourceId);
    UT_ASSERT_NOT_NULL_FATAL(pipeline.get());

    UT_ASSERT_TRUE(pipeline->setLowLatency(true));

    pipeline->stop();
    pipeline.reset();
}

/**
 * RC-CORE-PIPE-022 — setSync / getSync round-trip the clock-sync flag. sync is a
 * standard GstBaseSink property, present on every audio sink, so the round-trip
 * holds on all platforms (the write applies asynchronously; the read polls).
 */
UT_ADD_TEST(L4PipelineAuxDataPathTests, SyncRoundTrips)
{
    CONFORMANCE_CORE_TEST();

    AacElementaryStream stream = generateAacAdtsStream(kAuxFrames);
    UT_ASSERT_TRUE_FATAL(!stream.frames.empty());
    auto client = std::make_shared<FeedingMediaPipelineClient>();
    int32_t sourceId = -1;
    std::unique_ptr<IMediaPipeline> pipeline = driveToPlaying(client, stream, sourceId);
    UT_ASSERT_NOT_NULL_FATAL(pipeline.get());

    UT_ASSERT_TRUE(pipeline->setSync(true));
    bool sync = false;
    UT_ASSERT_TRUE(pollUntilApplied([&] { return pipeline->getSync(sync) && sync; }));

    pipeline->stop();
    pipeline.reset();
}

/**
 * RC-CORE-PIPE-023 — setSyncOff must be set before the pipeline reaches PLAYING:
 * the request made between attach and play is accepted (returns true). sync-off
 * is a vendor audio-sink property with no getter, so the accept contract is the
 * assertable clause.
 */
UT_ADD_TEST(L4PipelineAuxDataPathTests, SetSyncOffAcceptedBeforePlaying)
{
    CONFORMANCE_CORE_TEST();

    AacElementaryStream stream = generateAacAdtsStream(kAuxFrames);
    UT_ASSERT_TRUE_FATAL(!stream.frames.empty());
    auto client = std::make_shared<FeedingMediaPipelineClient>();

    auto factory = IMediaPipelineFactory::createFactory();
    UT_ASSERT_NOT_NULL_FATAL(factory.get());
    VideoRequirements requirements{kMaxWidth, kMaxHeight};
    std::unique_ptr<IMediaPipeline> pipeline = factory->createMediaPipeline(client, requirements);
    UT_ASSERT_NOT_NULL_FATAL(pipeline.get());
    client->setPipeline(pipeline.get());

    UT_ASSERT_TRUE(pipeline->load(MediaType::MSE, "", "mse://1", false));

    AudioConfig audioConfig;
    audioConfig.numberOfChannels = stream.channels;
    audioConfig.sampleRate = stream.sampleRate;
    std::unique_ptr<IMediaPipeline::MediaSource> source =
        std::make_unique<IMediaPipeline::MediaSourceAudio>("audio/mp4", false, audioConfig);
    UT_ASSERT_TRUE(pipeline->attachSource(source));
    client->addAudioSource(source->getId(), stream);

    // The documented window: before the pipeline reaches PLAYING.
    UT_ASSERT_TRUE(pipeline->setSyncOff(true));

    pipeline.reset();
}

/**
 * RC-CORE-PIPE-024 — setStreamSyncMode / getStreamSyncMode round-trip the
 * stream-sync mode where the platform's audio sink exposes the property; where
 * it does not (the software fake sink), the getter answers false consistently —
 * conform-when-supported, as with the buffering limit.
 */
UT_ADD_TEST(L4PipelineAuxDataPathTests, StreamSyncModeConformsWhenSupported)
{
    CONFORMANCE_CORE_TEST();

    AacElementaryStream stream = generateAacAdtsStream(kAuxFrames);
    UT_ASSERT_TRUE_FATAL(!stream.frames.empty());
    auto client = std::make_shared<FeedingMediaPipelineClient>();
    int32_t sourceId = -1;
    std::unique_ptr<IMediaPipeline> pipeline = driveToPlaying(client, stream, sourceId);
    UT_ASSERT_NOT_NULL_FATAL(pipeline.get());

    constexpr int32_t kMode = 1; // immediate next-frame sync
    UT_ASSERT_TRUE(pipeline->setStreamSyncMode(sourceId, kMode));

    int32_t readBack = -1;
    const bool supported =
        pollUntilApplied([&] { return pipeline->getStreamSyncMode(readBack) && readBack == kMode; });
    if (supported)
    {
        UT_LOG("[aux] stream-sync-mode round-tripped: %d", readBack);
        UT_ASSERT_EQUAL(readBack, kMode);
    }
    else
    {
        int32_t unsupportedRead = -1;
        UT_ASSERT_FALSE(pipeline->getStreamSyncMode(unsupportedRead));
        UT_LOG("[aux] stream-sync-mode unsupported by this platform's audio sink (conform-when-supported)");
    }

    pipeline->stop();
    pipeline.reset();
}

/**
 * RC-CORE-PIPE-031 — switchSource switches a media source stream: a new audio
 * source description supplied mid-playback is accepted (returns true) on the
 * realized pipeline.
 */
UT_ADD_TEST(L4PipelineAuxDataPathTests, SwitchSourceAcceptsNewAudioSource)
{
    CONFORMANCE_CORE_TEST();

    AacElementaryStream stream = generateAacAdtsStream(kAuxFrames);
    UT_ASSERT_TRUE_FATAL(!stream.frames.empty());
    auto client = std::make_shared<FeedingMediaPipelineClient>();
    int32_t sourceId = -1;
    std::unique_ptr<IMediaPipeline> pipeline = driveToPlaying(client, stream, sourceId);
    UT_ASSERT_NOT_NULL_FATAL(pipeline.get());

    AudioConfig audioConfig;
    audioConfig.numberOfChannels = stream.channels;
    audioConfig.sampleRate = stream.sampleRate;
    std::unique_ptr<IMediaPipeline::MediaSource> newSource =
        std::make_unique<IMediaPipeline::MediaSourceAudio>("audio/mp4", false, audioConfig);
    UT_ASSERT_TRUE(pipeline->switchSource(newSource));

    pipeline->stop();
    pipeline.reset();
}
