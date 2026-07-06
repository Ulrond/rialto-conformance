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
 * @file PlaybackDataPathTests.cpp
 *
 * L4 — end-to-end data-path cases for Surface B (native IMediaPipeline).
 *
 * Unlike the L1 IMediaPipeline cases (create + synchronous introspection), these
 * drive REAL elementary-stream buffers through a live RialtoServer: attach an
 * audio source, answer the server's need-data requests with decodable AAC
 * samples, and reach PLAYING. The stream is synthesised in-process (MediaFeed),
 * so the case is hermetic and needs no network asset; the software render path
 * (issue #18) decodes it headlessly through the fake sinks.
 *
 * These exercise the parts of the published IMediaPipeline contract that only
 * yield a value once a source is attached and the backend media pipeline is
 * realized — the getters that return false on a freshly-created pipeline.
 *
 * Coverage trace: coverage/rc-core-catalog.yaml / matrix.yaml — RC-CORE-PIPE-012
 * (getPosition returns true synchronously and writes the position out-param) and
 * RC-CORE-PIPE-014 (getStats writes rendered/dropped frame counts for a source id).
 */

#include <ut.h>
#include <ut_log.h>

#include "conformance/MediaFeed.h"
#include "conformance/Surfaces.h"
#include "conformance/TierGate.h"

#include "IMediaPipeline.h"

#include <chrono>
#include <memory>

using namespace firebolt::rialto;
using rialto::conformance::AacElementaryStream;
using rialto::conformance::FeedingMediaPipelineClient;
using rialto::conformance::generateAacAdtsStream;
using rialto::conformance::NativeClientSurface;

namespace
{
// A nominal decode ceiling for the pipeline session (the source under test is
// audio, but VideoRequirements is always supplied at creation).
constexpr uint32_t kMaxWidth = 1920;
constexpr uint32_t kMaxHeight = 1080;

// AAC access units to synthesise (~1s at 48 kHz). Enough for the server to reach
// and hold PLAYING while the getters are read.
constexpr int kAacFrames = 50;

// Upper bound on how long the server may take to reach PLAYING once play() is
// called and data is flowing, on the headless software render path.
constexpr std::chrono::milliseconds kPlayingTimeout{15000};

class L4PlaybackDataPathTests : public NativeClientSurface
{
};
} // namespace

UT_ADD_TEST_TO_GROUP(L4PlaybackDataPathTests, UT_TESTS_L4);

/**
 * RC-CORE-PIPE-012 — getPosition returns true synchronously and writes the
 * position out-param, once a source is attached and the backend media pipeline
 * is realized. On a freshly-created pipeline getPosition returns false (no
 * realized backend); here a real AAC source is attached, fed, and driven to
 * PLAYING, after which getPosition must succeed.
 */
UT_ADD_TEST(L4PlaybackDataPathTests, GetPositionSucceedsOncePlaying)
{
    CONFORMANCE_CORE_TEST();

    // Real AAC (ADTS) to feed. Baseline codec support is required, so failing to
    // synthesise it is a harness/platform fault, not a soft skip.
    AacElementaryStream stream = generateAacAdtsStream(kAacFrames);
    UT_LOG("[data-path] synthesised AAC frames=%zu rate=%u channels=%u totalDurationNs=%lld",
           stream.frames.size(), stream.sampleRate, stream.channels,
           static_cast<long long>(stream.totalDurationNs()));
    UT_ASSERT_TRUE_FATAL(!stream.frames.empty());

    auto client = std::make_shared<FeedingMediaPipelineClient>();

    auto factory = IMediaPipelineFactory::createFactory();
    UT_ASSERT_NOT_NULL_FATAL(factory.get());

    VideoRequirements requirements{kMaxWidth, kMaxHeight};
    std::unique_ptr<IMediaPipeline> pipeline = factory->createMediaPipeline(client, requirements);
    UT_ASSERT_NOT_NULL_FATAL(pipeline.get());
    client->setPipeline(pipeline.get());

    // Before the backend is realized, getPosition has no position to report and
    // returns false — the baseline the realized-pipeline contract is measured against.
    int64_t freshPosition = -1;
    UT_ASSERT_FALSE(pipeline->getPosition(freshPosition));

    // MSE load, then attach the clear AAC audio source.
    const bool loadOk = pipeline->load(MediaType::MSE, "", "mse://1", false);
    UT_ASSERT_TRUE(loadOk);

    AudioConfig audioConfig;
    audioConfig.numberOfChannels = stream.channels;
    audioConfig.sampleRate = stream.sampleRate;
    std::unique_ptr<IMediaPipeline::MediaSource> source =
        std::make_unique<IMediaPipeline::MediaSourceAudio>("audio/mp4", false, audioConfig);
    UT_ASSERT_TRUE(pipeline->attachSource(source));
    const int32_t sourceId = source->getId();
    client->addAudioSource(sourceId, stream);

    UT_ASSERT_TRUE(pipeline->allSourcesAttached());

    bool playAsync = false;
    UT_ASSERT_TRUE(pipeline->play(playAsync));

    // The feed answers the server's need-data requests with the AAC samples; the
    // server decodes them headlessly and reaches PLAYING.
    UT_ASSERT_TRUE_FATAL(client->waitForPlaybackState(PlaybackState::PLAYING, kPlayingTimeout));
    UT_ASSERT_FALSE(client->sawPlaybackState(PlaybackState::FAILURE));

    // The contract under test: on the realized, playing pipeline getPosition
    // returns true and writes a non-negative position out-param.
    int64_t position = -1;
    const bool getPositionOk = pipeline->getPosition(position);
    UT_LOG("[data-path] getPosition once playing: ok=%d pos=%lld", getPositionOk,
           static_cast<long long>(position));
    UT_ASSERT_TRUE(getPositionOk);
    UT_ASSERT_TRUE(position >= 0);

    // Tear the data path down before the client is released: stop playback, then
    // destroy the pipeline (which ends need-data callbacks) while the client that
    // services them is still alive.
    pipeline->stop();
    pipeline.reset();
}

/**
 * RC-CORE-PIPE-014 — getStats writes rendered and dropped frame counts for a
 * source id, once that source is attached and the backend media pipeline is
 * realized and playing. On a freshly-created pipeline getStats returns false (no
 * realized backend / no such source); here a real AAC source is attached, fed,
 * and driven to PLAYING, after which getStats must succeed for that source id and
 * write both counters.
 */
UT_ADD_TEST(L4PlaybackDataPathTests, GetStatsSucceedsOncePlaying)
{
    CONFORMANCE_CORE_TEST();

    // Real AAC (ADTS) to feed. Baseline codec support is required, so failing to
    // synthesise it is a harness/platform fault, not a soft skip.
    AacElementaryStream stream = generateAacAdtsStream(kAacFrames);
    UT_LOG("[data-path] synthesised AAC frames=%zu rate=%u channels=%u totalDurationNs=%lld",
           stream.frames.size(), stream.sampleRate, stream.channels,
           static_cast<long long>(stream.totalDurationNs()));
    UT_ASSERT_TRUE_FATAL(!stream.frames.empty());

    auto client = std::make_shared<FeedingMediaPipelineClient>();

    auto factory = IMediaPipelineFactory::createFactory();
    UT_ASSERT_NOT_NULL_FATAL(factory.get());

    VideoRequirements requirements{kMaxWidth, kMaxHeight};
    std::unique_ptr<IMediaPipeline> pipeline = factory->createMediaPipeline(client, requirements);
    UT_ASSERT_NOT_NULL_FATAL(pipeline.get());
    client->setPipeline(pipeline.get());

    // Before the backend is realized (and before any source exists), getStats has
    // no source to report on and returns false — the baseline the realized-pipeline
    // contract is measured against.
    uint64_t freshRendered = 0;
    uint64_t freshDropped = 0;
    UT_ASSERT_FALSE(pipeline->getStats(0, freshRendered, freshDropped));

    // MSE load, then attach the clear AAC audio source.
    const bool loadOk = pipeline->load(MediaType::MSE, "", "mse://1", false);
    UT_ASSERT_TRUE(loadOk);

    AudioConfig audioConfig;
    audioConfig.numberOfChannels = stream.channels;
    audioConfig.sampleRate = stream.sampleRate;
    std::unique_ptr<IMediaPipeline::MediaSource> source =
        std::make_unique<IMediaPipeline::MediaSourceAudio>("audio/mp4", false, audioConfig);
    UT_ASSERT_TRUE(pipeline->attachSource(source));
    const int32_t sourceId = source->getId();
    client->addAudioSource(sourceId, stream);

    UT_ASSERT_TRUE(pipeline->allSourcesAttached());

    bool playAsync = false;
    UT_ASSERT_TRUE(pipeline->play(playAsync));

    // The feed answers the server's need-data requests with the AAC samples; the
    // server decodes them headlessly and reaches PLAYING.
    UT_ASSERT_TRUE_FATAL(client->waitForPlaybackState(PlaybackState::PLAYING, kPlayingTimeout));
    UT_ASSERT_FALSE(client->sawPlaybackState(PlaybackState::FAILURE));

    // The contract under test: on the realized, playing pipeline getStats returns
    // true for the attached source id and writes both frame counters. Dropped
    // frames must never exceed rendered.
    uint64_t renderedFrames = 0;
    uint64_t droppedFrames = 0;
    const bool getStatsOk = pipeline->getStats(sourceId, renderedFrames, droppedFrames);
    UT_LOG("[data-path] getStats once playing: ok=%d sourceId=%d rendered=%llu dropped=%llu",
           getStatsOk, sourceId, static_cast<unsigned long long>(renderedFrames),
           static_cast<unsigned long long>(droppedFrames));
    UT_ASSERT_TRUE(getStatsOk);
    UT_ASSERT_TRUE(droppedFrames <= renderedFrames);

    // Tear the data path down before the client is released: stop playback, then
    // destroy the pipeline (which ends need-data callbacks) while the client that
    // services them is still alive.
    pipeline->stop();
    pipeline.reset();
}
