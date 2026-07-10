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
 * @file PipelineTransportDataPathTests.cpp
 *
 * L4 — end-to-end data-path cases for the Surface B (native IMediaPipeline)
 * transport surface: attach/remove source, the allSourcesAttached gate, and the
 * play/pause/stop playback state machine. These only exercise meaningfully once a
 * source is attached and the backend media pipeline is realized, so each drives a
 * real synthesised AAC source through a live RialtoServer (MediaFeed), hermetic
 * and headless via the software render path (issue #18).
 *
 * The state-transition cases assert both the synchronous return contract and the
 * asynchronous PlaybackState notification the transition produces (observed via
 * the IMediaPipelineClient callback) — the expected sequence is the one Rialto's
 * own component tests encode (tests/componenttests/.../mse; see
 * coverage/interface-definition-gaps.md IDG-004), asserted here as the CORE
 * transform-safety contract.
 *
 * Coverage trace: coverage/rc-core-catalog.yaml / matrix.yaml —
 * RC-CORE-PIPE-003 (load), -004 (attachSource id), -005 (removeSource),
 * -006 (allSourcesAttached once), -007 (play/PLAYING), -008 (pause/PAUSED),
 * -009 (stop/STOPPED), -010 (setPlaybackRate), -011 (setPosition/seek).
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
// A nominal decode ceiling for the pipeline session (audio source under test, but
// VideoRequirements is always supplied at creation).
constexpr uint32_t kMaxWidth = 1920;
constexpr uint32_t kMaxHeight = 1080;

// A longer AAC run (~4s at 48 kHz) than the getter cases: it gives the playout a
// comfortable window between PLAYING and end-of-stream so pause() lands while the
// stream is still live (and PAUSED then freezes playout before stop()).
constexpr int kLifecycleFrames = 200;

// Upper bound on how long the server may take to reach a playback state on the
// headless software render path.
constexpr std::chrono::milliseconds kPlayingTimeout{15000};
constexpr std::chrono::milliseconds kStateTimeout{15000};

// pause()/stop() are documented MUST-NOT-block: the synchronous call enqueues the
// transition and returns; it does not wait out the async state change. A generous
// bound distinguishes "returns promptly" from "blocks on the full transition"
// without being flaky on a loaded runner.
constexpr std::chrono::milliseconds kNonBlockBound{5000};

/**
 * Create a pipeline bound to @p client, load MSE, and attach the AAC audio source;
 * write the attached source id. Leaves allSourcesAttached()/play() to the caller
 * so each case drives the transport step it asserts. Returns nullptr if any setup
 * step fails (the caller fatal-asserts the result).
 */
std::unique_ptr<IMediaPipeline> loadAndAttachAudio(const std::shared_ptr<FeedingMediaPipelineClient> &client,
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
    return pipeline;
}

class L4PipelineTransportDataPathTests : public NativeClientSurface
{
};
} // namespace

UT_ADD_TEST_TO_GROUP(L4PipelineTransportDataPathTests, UT_TESTS_L4);

/**
 * RC-CORE-PIPE-003 + RC-CORE-PIPE-004 — load(MSE, ...) returns true for a
 * supported source type, and attachSource is valid for MediaType::MSE and assigns
 * a source id retrievable via MediaSource::getId(). The returned id is the handle
 * every later transport/data call uses, so it must be a real, stable value.
 */
UT_ADD_TEST(L4PipelineTransportDataPathTests, LoadAndAttachAssignRetrievableSourceId)
{
    CONFORMANCE_CORE_TEST();

    AacElementaryStream stream = generateAacAdtsStream(kLifecycleFrames);
    UT_ASSERT_TRUE_FATAL(!stream.frames.empty());

    auto client = std::make_shared<FeedingMediaPipelineClient>();

    auto factory = IMediaPipelineFactory::createFactory();
    UT_ASSERT_NOT_NULL_FATAL(factory.get());
    VideoRequirements requirements{kMaxWidth, kMaxHeight};
    std::unique_ptr<IMediaPipeline> pipeline = factory->createMediaPipeline(client, requirements);
    UT_ASSERT_NOT_NULL_FATAL(pipeline.get());
    client->setPipeline(pipeline.get());

    // PIPE-003: load MSE for a supported source type.
    UT_ASSERT_TRUE(pipeline->load(MediaType::MSE, "", "mse://1", false));

    // PIPE-004: attachSource assigns a retrievable id.
    AudioConfig audioConfig;
    audioConfig.numberOfChannels = stream.channels;
    audioConfig.sampleRate = stream.sampleRate;
    std::unique_ptr<IMediaPipeline::MediaSource> source =
        std::make_unique<IMediaPipeline::MediaSourceAudio>("audio/mp4", false, audioConfig);
    UT_ASSERT_TRUE(pipeline->attachSource(source));
    const int32_t sourceId = source->getId();
    UT_LOG("[transport] attached audio source id=%d", sourceId);
    UT_ASSERT_TRUE(sourceId >= 0);

    pipeline.reset();
}

/**
 * RC-CORE-PIPE-005 — removeSource(id) returns true for a previously attached
 * source id.
 */
UT_ADD_TEST(L4PipelineTransportDataPathTests, RemoveSourceSucceedsForAttachedSource)
{
    CONFORMANCE_CORE_TEST();

    AacElementaryStream stream = generateAacAdtsStream(kLifecycleFrames);
    UT_ASSERT_TRUE_FATAL(!stream.frames.empty());

    auto client = std::make_shared<FeedingMediaPipelineClient>();
    int32_t sourceId = -1;
    std::unique_ptr<IMediaPipeline> pipeline = loadAndAttachAudio(client, stream, sourceId);
    UT_ASSERT_NOT_NULL_FATAL(pipeline.get());

    UT_ASSERT_TRUE(pipeline->removeSource(sourceId));

    pipeline.reset();
}

/**
 * RC-CORE-PIPE-006 — allSourcesAttached must be called before streaming and may be
 * called only once: the first call returns true, a second call is illegal and
 * returns false (the server guards it with m_wasAllSourcesAttachedCalled).
 */
UT_ADD_TEST(L4PipelineTransportDataPathTests, AllSourcesAttachedIsLegalOnlyOnce)
{
    CONFORMANCE_CORE_TEST();

    AacElementaryStream stream = generateAacAdtsStream(kLifecycleFrames);
    UT_ASSERT_TRUE_FATAL(!stream.frames.empty());

    auto client = std::make_shared<FeedingMediaPipelineClient>();
    int32_t sourceId = -1;
    std::unique_ptr<IMediaPipeline> pipeline = loadAndAttachAudio(client, stream, sourceId);
    UT_ASSERT_NOT_NULL_FATAL(pipeline.get());

    UT_ASSERT_TRUE(pipeline->allSourcesAttached());
    // A second call is illegal and must be rejected.
    UT_ASSERT_FALSE(pipeline->allSourcesAttached());

    pipeline->stop();
    pipeline.reset();
}

/**
 * RC-CORE-PIPE-007 / -008 / -009 — the play/pause/stop state machine. play and
 * pause return true synchronously AND produce their PlaybackState notification
 * (play -> PLAYING, pause -> PAUSED). stop() is asserted on its synchronous
 * contract only (returns true, must not block): the STOPPED notification rides a
 * GStreamer bus state-changed(NULL) message that the pipeline's default
 * auto-flush-bus behaviour races against — gst_element_set_state(NULL) flushes
 * the bus synchronously while the server's dispatcher polls it, so on a live
 * backend the message is usually dropped. Rialto's own component tests only
 * observe STOPPED by injecting the message through a mocked gst wrapper, so
 * delivery is not a live-pipeline guarantee (interface-definition-gaps.md
 * IDG-005). The case polls briefly and logs whether STOPPED arrived, without
 * asserting it. pause() and stop() are documented MUST-NOT-block, so their
 * synchronous call time is bounded well under the async transition timeout.
 */
UT_ADD_TEST(L4PipelineTransportDataPathTests, PlayPauseStopStateMachine)
{
    CONFORMANCE_CORE_TEST();

    AacElementaryStream stream = generateAacAdtsStream(kLifecycleFrames);
    UT_ASSERT_TRUE_FATAL(!stream.frames.empty());

    auto client = std::make_shared<FeedingMediaPipelineClient>();
    int32_t sourceId = -1;
    std::unique_ptr<IMediaPipeline> pipeline = loadAndAttachAudio(client, stream, sourceId);
    UT_ASSERT_NOT_NULL_FATAL(pipeline.get());

    UT_ASSERT_TRUE(pipeline->allSourcesAttached());

    // PIPE-007: play returns true, sets the async out-param, backend reaches PLAYING.
    bool async = false;
    UT_ASSERT_TRUE(pipeline->play(async));
    UT_ASSERT_TRUE_FATAL(client->waitForPlaybackState(PlaybackState::PLAYING, kPlayingTimeout));
    UT_ASSERT_FALSE(client->sawPlaybackState(PlaybackState::FAILURE));

    // PIPE-008: pause returns true (promptly), backend reaches PAUSED.
    auto pauseStart = std::chrono::steady_clock::now();
    const bool pauseOk = pipeline->pause();
    auto pauseElapsed = std::chrono::steady_clock::now() - pauseStart;
    UT_ASSERT_TRUE(pauseOk);
    UT_ASSERT_TRUE(client->waitForPlaybackState(PlaybackState::PAUSED, kStateTimeout));
    UT_ASSERT_TRUE(pauseElapsed < kNonBlockBound);

    // PIPE-009: stop returns true (promptly). The STOPPED notification is
    // observed-if-delivered, not asserted — the bus state-changed(NULL) message it
    // rides is racy against the pipeline's bus auto-flush on a live backend
    // (IDG-005); no real client can rely on it.
    auto stopStart = std::chrono::steady_clock::now();
    const bool stopOk = pipeline->stop();
    auto stopElapsed = std::chrono::steady_clock::now() - stopStart;
    UT_ASSERT_TRUE(stopOk);
    UT_ASSERT_TRUE(stopElapsed < kNonBlockBound);
    const bool sawStopped = client->waitForPlaybackState(PlaybackState::STOPPED, std::chrono::milliseconds{2000});

    UT_LOG("[transport] play/pause/stop state machine OK (pause %lld ms, stop %lld ms, STOPPED delivered=%d)",
           static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(pauseElapsed).count()),
           static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(stopElapsed).count()),
           sawStopped);

    pipeline.reset();
}

/**
 * RC-CORE-PIPE-010 — setPlaybackRate(rate) returns true for a backend-supported
 * rate. The server applies a rate only once the pipeline is PLAYING (below that
 * it postpones), so the case drives to PLAYING first and then sets 2.0 — the
 * documented custom-instant-rate-change path Rialto's component test encodes.
 */
UT_ADD_TEST(L4PipelineTransportDataPathTests, SetPlaybackRateSucceedsWhilePlaying)
{
    CONFORMANCE_CORE_TEST();

    AacElementaryStream stream = generateAacAdtsStream(kLifecycleFrames);
    UT_ASSERT_TRUE_FATAL(!stream.frames.empty());

    auto client = std::make_shared<FeedingMediaPipelineClient>();
    int32_t sourceId = -1;
    std::unique_ptr<IMediaPipeline> pipeline = loadAndAttachAudio(client, stream, sourceId);
    UT_ASSERT_NOT_NULL_FATAL(pipeline.get());

    UT_ASSERT_TRUE(pipeline->allSourcesAttached());
    bool async = false;
    UT_ASSERT_TRUE(pipeline->play(async));
    UT_ASSERT_TRUE_FATAL(client->waitForPlaybackState(PlaybackState::PLAYING, kPlayingTimeout));

    UT_ASSERT_TRUE(pipeline->setPlaybackRate(2.0));

    pipeline->stop();
    pipeline.reset();
}

/**
 * RC-CORE-PIPE-011 — setPosition's two documented branches. Before playback
 * starts it sets the start position and returns true (no seek). After playback
 * has started it performs a seek: MUST NOT block, the backend notifies
 * PlaybackState::SEEKING and then SEEK_DONE once the seek completes. The feed is
 * rewound to the seek target (start) as an MSE app re-appends data from there.
 */
UT_ADD_TEST(L4PipelineTransportDataPathTests, SetPositionSetsStartThenSeeksWhilePlaying)
{
    CONFORMANCE_CORE_TEST();

    AacElementaryStream stream = generateAacAdtsStream(kLifecycleFrames);
    UT_ASSERT_TRUE_FATAL(!stream.frames.empty());

    auto client = std::make_shared<FeedingMediaPipelineClient>();
    int32_t sourceId = -1;
    std::unique_ptr<IMediaPipeline> pipeline = loadAndAttachAudio(client, stream, sourceId);
    UT_ASSERT_NOT_NULL_FATAL(pipeline.get());

    // Branch 1: before playback starts, setPosition sets the start position.
    UT_ASSERT_TRUE(pipeline->setPosition(0));

    UT_ASSERT_TRUE(pipeline->allSourcesAttached());
    bool async = false;
    UT_ASSERT_TRUE(pipeline->play(async));
    UT_ASSERT_TRUE_FATAL(client->waitForPlaybackState(PlaybackState::PLAYING, kPlayingTimeout));

    // Branch 2: after start, setPosition seeks. Must not block; the backend
    // notifies SEEKING then SEEK_DONE. Rewind the feed so the re-requested data
    // starts from the seek target, as an MSE app re-appending from there would.
    auto seekStart = std::chrono::steady_clock::now();
    const bool seekOk = pipeline->setPosition(0);
    auto seekElapsed = std::chrono::steady_clock::now() - seekStart;
    client->rewindSource(sourceId);
    UT_ASSERT_TRUE(seekOk);
    UT_ASSERT_TRUE(seekElapsed < kNonBlockBound);

    UT_ASSERT_TRUE(client->waitForPlaybackState(PlaybackState::SEEKING, kStateTimeout));
    UT_ASSERT_TRUE(client->waitForPlaybackState(PlaybackState::SEEK_DONE, kStateTimeout));
    UT_ASSERT_FALSE(client->sawPlaybackState(PlaybackState::FAILURE));

    UT_LOG("[transport] setPosition: start-position branch OK; seek SEEKING->SEEK_DONE OK (call %lld ms)",
           static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(seekElapsed).count()));

    pipeline->stop();
    pipeline.reset();
}
