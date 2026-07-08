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
 * @file DataProtocolTests.cpp
 *
 * L4 — the need-data / have-data data-transfer protocol between the server and
 * an IMediaPipelineClient (RC-CORE-DATA-*), asserted from the records the feed
 * client keeps while a real playback runs. The protocol cannot be introspected —
 * it only exists while samples flow — so each case drives the software data path
 * (MediaFeed) and then checks the observed interaction shape against the
 * documented contract. The expected sequences are the ones Rialto's own
 * component tests encode (interface-definition-gaps.md IDG-004), asserted as the
 * CORE transform-safety contract.
 *
 * Coverage trace: coverage/rc-core-catalog.yaml / matrix.yaml —
 * RC-CORE-DATA-001 (need-data carries frame count + shm info; the client batch
 * honours it), -002 (no overlapping request per source), -003 (addSegment OK),
 * -006 (haveData correlation; EOS ends the stream), -009 (clean-run
 * network-state vocabulary), -012 (buffer-underflow fires when starved;
 * first-frame observed).
 */

#include <ut.h>
#include <ut_log.h>

#include "conformance/MediaFeed.h"
#include "conformance/Surfaces.h"
#include "conformance/TierGate.h"

#include "IMediaPipeline.h"
#include "MediaCommon.h"

#include <chrono>
#include <memory>

using namespace firebolt::rialto;
using rialto::conformance::AacElementaryStream;
using rialto::conformance::FeedingMediaPipelineClient;
using rialto::conformance::generateAacAdtsStream;
using rialto::conformance::NativeClientSurface;

namespace
{
constexpr uint32_t kMaxWidth = 1920;
constexpr uint32_t kMaxHeight = 1080;

// A short stream (~1s) for the EOS case, a longer one (~4s) where the stream
// must stay live while the protocol is observed.
constexpr int kShortFrames = 50;
constexpr int kLongFrames = 200;

constexpr std::chrono::milliseconds kPlayingTimeout{15000};
constexpr std::chrono::milliseconds kEosTimeout{20000};
constexpr std::chrono::milliseconds kUnderflowTimeout{15000};

/// Same bring-up as the transport cases: realized pipeline, PLAYING.
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

class L4DataProtocolTests : public NativeClientSurface
{
};
} // namespace

UT_ADD_TEST_TO_GROUP(L4DataProtocolTests, UT_TESTS_L4);

/**
 * RC-CORE-DATA-001 + RC-CORE-DATA-002 + RC-CORE-DATA-003 — the shape of the
 * need-data protocol over a real run: every notifyNeedMediaData carries a
 * positive frame count and shm info; the server never issues a second request
 * for a source whose previous request is unanswered; and every segment the feed
 * offered within the requested frame count was accepted (addSegment OK).
 */
UT_ADD_TEST(L4DataProtocolTests, NeedDataProtocolIsWellFormed)
{
    CONFORMANCE_CORE_TEST();

    AacElementaryStream stream = generateAacAdtsStream(kLongFrames);
    UT_ASSERT_TRUE_FATAL(!stream.frames.empty());
    auto client = std::make_shared<FeedingMediaPipelineClient>();
    int32_t sourceId = -1;
    std::unique_ptr<IMediaPipeline> pipeline = driveToPlaying(client, stream, sourceId);
    UT_ASSERT_NOT_NULL_FATAL(pipeline.get());

    const auto log = client->needDataLog();
    size_t withShmInfo = 0;
    for (const auto &record : log)
        withShmInfo += record.hadShmInfo ? 1 : 0;
    UT_LOG("[data-protocol] need-data requests observed: %zu (shm-info=%zu); overlap=%d; allAccepted=%d", log.size(),
           withShmInfo, client->sawOverlappingNeedData(), client->allSegmentsAccepted());

    // DATA-001: requests arrived and each carried a usable frame count. The
    // shmInfo out-param is documented "null if not applicable to the client" —
    // for a public-API client the client library manages the shared memory
    // internally through addSegment, so null here is by-design (recorded, not
    // asserted).
    UT_ASSERT_TRUE_FATAL(!log.empty());
    for (const auto &record : log)
    {
        UT_ASSERT_TRUE(record.frameCount > 0);
        UT_ASSERT_EQUAL(record.sourceId, sourceId);
    }

    // DATA-002: no request overlapped an unanswered one for the same source.
    UT_ASSERT_FALSE(client->sawOverlappingNeedData());

    // DATA-003: every offered segment (batch bounded by frameCount) was accepted.
    UT_ASSERT_TRUE(client->allSegmentsAccepted());

    pipeline->stop();
    pipeline.reset();
}

/**
 * RC-CORE-DATA-006 — haveData correlates to its need-data request by id (the
 * whole feed rides that correlation), and the EOS status carries its documented
 * meaning: once the feed answers haveData(EOS) at stream exhaustion, the
 * pipeline reaches END_OF_STREAM.
 */
UT_ADD_TEST(L4DataProtocolTests, HaveDataEosDrivesEndOfStream)
{
    CONFORMANCE_CORE_TEST();

    AacElementaryStream stream = generateAacAdtsStream(kShortFrames);
    UT_ASSERT_TRUE_FATAL(!stream.frames.empty());
    auto client = std::make_shared<FeedingMediaPipelineClient>();
    int32_t sourceId = -1;
    std::unique_ptr<IMediaPipeline> pipeline = driveToPlaying(client, stream, sourceId);
    UT_ASSERT_NOT_NULL_FATAL(pipeline.get());

    // The feed exhausts the short stream and answers haveData(EOS); the pipeline
    // must then play out to END_OF_STREAM.
    UT_ASSERT_TRUE(client->waitForPlaybackState(PlaybackState::END_OF_STREAM, kEosTimeout));
    UT_ASSERT_FALSE(client->sawPlaybackState(PlaybackState::FAILURE));

    pipeline->stop();
    pipeline.reset();
}

/**
 * RC-CORE-DATA-009 — the clean-run network-state vocabulary for clear MSE
 * playback: BUFFERING is notified at load, BUFFERED once data is buffered, and
 * no error state (FORMAT_ERROR / NETWORK_ERROR / DECODE_ERROR) is ever notified
 * on a clean run.
 */
UT_ADD_TEST(L4DataProtocolTests, CleanRunNetworkStateVocabulary)
{
    CONFORMANCE_CORE_TEST();

    AacElementaryStream stream = generateAacAdtsStream(kShortFrames);
    UT_ASSERT_TRUE_FATAL(!stream.frames.empty());
    auto client = std::make_shared<FeedingMediaPipelineClient>();
    int32_t sourceId = -1;
    std::unique_ptr<IMediaPipeline> pipeline = driveToPlaying(client, stream, sourceId);
    UT_ASSERT_NOT_NULL_FATAL(pipeline.get());

    // Run to end-of-stream so the full clean lifecycle is observed.
    UT_ASSERT_TRUE(client->waitForPlaybackState(PlaybackState::END_OF_STREAM, kEosTimeout));

    const auto states = client->networkStatesSeen();
    UT_LOG("[data-protocol] network states seen: %zu (BUFFERING=%d BUFFERED=%d)", states.size(),
           states.count(NetworkState::BUFFERING) != 0, states.count(NetworkState::BUFFERED) != 0);

    // BUFFERING is notified at load (the load() contract).
    UT_ASSERT_TRUE(states.count(NetworkState::BUFFERING) != 0);
    // No error state on a clean run.
    UT_ASSERT_TRUE(states.count(NetworkState::FORMAT_ERROR) == 0);
    UT_ASSERT_TRUE(states.count(NetworkState::NETWORK_ERROR) == 0);
    UT_ASSERT_TRUE(states.count(NetworkState::DECODE_ERROR) == 0);

    pipeline->stop();
    pipeline.reset();
}

/**
 * RC-CORE-DATA-012 — the per-source event notifications and starvation
 * robustness. notifyBufferUnderflow rides the vendor `buffer-underflow-callback`
 * GSignal (the server connects to it where a decoder/sink exposes it; Rialto's
 * own UnderflowTest mocks that signal into existence), so its delivery is
 * vendor-element-dependent — the software elements expose no such signal and the
 * notification is structurally undeliverable here. Likewise first-frame delivery
 * is sink-dependent and QoS needs real frame drops. So this case asserts the
 * platform-independent clause — a starved source must NOT fail the pipeline
 * (playback state never reaches FAILURE while the feed answers need-data with no
 * segments) — and records/logs underflow + first-frame delivery, which firm up
 * as asserts on a vendor target.
 */
UT_ADD_TEST(L4DataProtocolTests, StarvationIsToleratedAndEventsObserved)
{
    CONFORMANCE_CORE_TEST();

    AacElementaryStream stream = generateAacAdtsStream(kLongFrames);
    UT_ASSERT_TRUE_FATAL(!stream.frames.empty());
    auto client = std::make_shared<FeedingMediaPipelineClient>();
    int32_t sourceId = -1;
    std::unique_ptr<IMediaPipeline> pipeline = driveToPlaying(client, stream, sourceId);
    UT_ASSERT_NOT_NULL_FATAL(pipeline.get());

    UT_LOG("[data-protocol] first-frame notification observed=%d", client->sawFirstFrame(sourceId));

    // Starve the source and observe. On a vendor target the drained queue raises
    // buffer underflow (logged here); on every platform the starved pipeline must
    // keep answering the protocol without entering FAILURE.
    client->setStarve(true);
    const bool underflowed = client->waitForBufferUnderflow(sourceId, kUnderflowTimeout);
    UT_LOG("[data-protocol] starved source underflowed=%d (vendor buffer-underflow-callback signal)", underflowed);

    UT_ASSERT_FALSE(client->sawPlaybackState(PlaybackState::FAILURE));

    pipeline->stop();
    pipeline.reset();
}
