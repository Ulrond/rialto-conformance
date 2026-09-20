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
 * @file DataFaultTests.cpp
 *
 * L4 — the data-transfer protocol's failure clauses (RC-CORE-DATA-004/005/007/
 * 010), driven from the public client API. The conditions these requirements
 * describe live inside the server, but the client decides what it offers and
 * when, so a feed that deliberately misbehaves reaches them: offering more than
 * a round can hold exhausts the shared buffer, quoting a request id the server
 * never issued is an unexpected call, leaving a request unanswered gives the
 * server something to cancel, and an undecodable access unit faults the decoder
 * while the stream keeps flowing.
 *
 * Each case asserts the clause that holds on every platform and records what is
 * platform-variable, the same division the starvation case uses: a server that
 * cannot be pushed into a condition is not a failure of the client, but a server
 * that reaches it and then behaves differently from the contract is a finding.
 *
 * Coverage trace: coverage/rc-core-catalog.yaml / matrix.yaml —
 * RC-CORE-DATA-004 (NO_SPACE is not an error; the segment is retained and
 * resent), -005 (ERROR on an unexpected call), -007 (a cancelled need-data is
 * not an error), -010 (a non-fatal playback error leaves PlaybackState
 * unchanged).
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

constexpr int kLongFrames = 400;

// The server's audio partition of the shared buffer is 1MB, and it drains as the
// decoder consumes. Filling it therefore takes a round that offers more than a
// megabyte of access units at once: enough frames to exceed it, and an oversupply
// large enough that a single need-data offers them all.
constexpr int kFillFrames = 6000;
constexpr size_t kFillOversupply = 6000;

constexpr std::chrono::milliseconds kPlayingTimeout{15000};
constexpr std::chrono::milliseconds kSettleTime{4000};
constexpr std::chrono::milliseconds kCancelTimeout{5000};

/// A request id the server cannot have issued: ids are handed out per need-data,
/// and this run has answered every one it saw.
constexpr uint32_t kUnissuedRequestId = 0x7FFFFFF0u;

/// Same bring-up as the other data-path cases: realized pipeline, PLAYING.
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

const char *addStatusName(AddSegmentStatus status)
{
    switch (status)
    {
    case AddSegmentStatus::OK:
        return "OK";
    case AddSegmentStatus::NO_SPACE:
        return "NO_SPACE";
    case AddSegmentStatus::ERROR:
        return "ERROR";
    }
    return "?";
}

class L4DataFaultTests : public NativeClientSurface
{
};
} // namespace

UT_ADD_TEST_TO_GROUP(L4DataFaultTests, UT_TESTS_L4);

/**
 * RC-CORE-DATA-004 — NO_SPACE is not an error. The feed offers many times the
 * requested frame count on every need-data, so the shared buffer runs out within
 * a round. What the server rejects for want of space must be retained by the
 * client and offered again on the next request, and playback must continue: a
 * full buffer is back-pressure, not a failure.
 */
UT_ADD_TEST(L4DataFaultTests, NoSpaceIsNotAnErrorAndTheSegmentIsResent)
{
    CONFORMANCE_CORE_TEST();

    AacElementaryStream stream = generateAacAdtsStream(kFillFrames);
    UT_ASSERT_TRUE_FATAL(!stream.frames.empty());
    auto client = std::make_shared<FeedingMediaPipelineClient>();
    client->setOversupply(kFillOversupply); // offer the whole stream in one round
    int32_t sourceId = -1;
    std::unique_ptr<IMediaPipeline> pipeline = driveToPlaying(client, stream, sourceId);
    UT_ASSERT_NOT_NULL_FATAL(pipeline.get());

    // Let several need-data rounds go by, so a segment rejected in one round has
    // the chance to be accepted in a later one.
    std::this_thread::sleep_for(kSettleTime);

    const size_t noSpace = client->addSegmentNoSpaceCount();
    const size_t errors = client->addSegmentErrorCount();
    UT_LOG("[data-fault] oversupplied feed: addSegment OK=%zu NO_SPACE=%zu ERROR=%zu; rejected-then-accepted=%d",
           client->addSegmentOkCount(), noSpace, errors, client->sawNoSpaceSegmentAccepted());

    // Whatever the buffer geometry, exhausting it is never reported as ERROR and
    // never fails the pipeline.
    UT_ASSERT_EQUAL(errors, 0u);
    UT_ASSERT_FALSE(client->sawPlaybackState(PlaybackState::FAILURE));

    if (noSpace > 0)
    {
        // The contract's second half: the rejected segment was kept and accepted
        // later, so no media was lost to back-pressure.
        UT_ASSERT_TRUE(client->sawNoSpaceSegmentAccepted());
        UT_ASSERT_TRUE(client->addSegmentOkCount() > 0);
    }
    else
    {
        // A buffer this feed could not fill. Recorded rather than asserted — the
        // partition size is a platform property, not an interface one.
        UT_LOG("[data-fault] the shared buffer absorbed %zu bytes offered at once; NO_SPACE not reached here",
               stream.totalBytes());
    }

    pipeline->stop();
    pipeline.reset();
}

/**
 * RC-CORE-DATA-005 — an unexpected call is reported as ERROR. addSegment quoting
 * a need-data request id the server never issued is exactly that: it is not a
 * full buffer, so it must not be reported as NO_SPACE, and it is not a segment
 * the server asked for, so it must not be accepted.
 */
UT_ADD_TEST(L4DataFaultTests, AddSegmentWithAnUnissuedRequestIdIsAnError)
{
    CONFORMANCE_CORE_TEST();

    AacElementaryStream stream = generateAacAdtsStream(kLongFrames);
    UT_ASSERT_TRUE_FATAL(!stream.frames.empty());
    auto client = std::make_shared<FeedingMediaPipelineClient>();
    int32_t sourceId = -1;
    std::unique_ptr<IMediaPipeline> pipeline = driveToPlaying(client, stream, sourceId);
    UT_ASSERT_NOT_NULL_FATAL(pipeline.get());

    // A well-formed segment, offered against a request that does not exist.
    const auto &frame = stream.frames.front();
    std::unique_ptr<IMediaPipeline::MediaSegment> segment =
        std::make_unique<IMediaPipeline::MediaSegmentAudio>(sourceId, frame.timeStamp, frame.duration,
                                                            static_cast<int32_t>(stream.sampleRate),
                                                            static_cast<int32_t>(stream.channels));
    segment->setData(static_cast<uint32_t>(frame.data.size()), frame.data.data());

    const AddSegmentStatus status = pipeline->addSegment(kUnissuedRequestId, segment);
    UT_LOG("[data-fault] addSegment(unissued request id 0x%X) -> %s", kUnissuedRequestId, addStatusName(status));

    UT_ASSERT_TRUE(status != AddSegmentStatus::OK);
    UT_ASSERT_EQUAL(static_cast<int>(status), static_cast<int>(AddSegmentStatus::ERROR));

    // Rejecting the stray call must not disturb the run in progress.
    UT_ASSERT_FALSE(client->sawPlaybackState(PlaybackState::FAILURE));

    pipeline->stop();
    pipeline.reset();
}

/**
 * RC-CORE-DATA-007 — a cancelled need-data is not an error. The feed leaves a
 * request outstanding and the source is then flushed, which is the point at which
 * the data the server asked for stops being wanted. Whether the server announces
 * that with notifyCancelNeedMediaData is its business; what conformance requires
 * is that the unanswered request costs the client nothing — no failure, and the
 * protocol resumes afterwards.
 */
UT_ADD_TEST(L4DataFaultTests, CancelledNeedDataIsNotAnError)
{
    CONFORMANCE_CORE_TEST();

    AacElementaryStream stream = generateAacAdtsStream(kLongFrames);
    UT_ASSERT_TRUE_FATAL(!stream.frames.empty());
    auto client = std::make_shared<FeedingMediaPipelineClient>();
    int32_t sourceId = -1;
    std::unique_ptr<IMediaPipeline> pipeline = driveToPlaying(client, stream, sourceId);
    UT_ASSERT_NOT_NULL_FATAL(pipeline.get());

    const size_t requestsBefore = client->needDataLog().size();

    // Stop answering, so a request is left outstanding, then flush the source.
    client->setWithhold(true);
    std::this_thread::sleep_for(kCancelTimeout);

    bool flushAsync = false;
    const bool flushed = pipeline->flush(sourceId, true, flushAsync);
    UT_LOG("[data-fault] flush with a need-data outstanding: accepted=%d async=%d", flushed, flushAsync);

    std::this_thread::sleep_for(kCancelTimeout);
    UT_LOG("[data-fault] cancels observed=%zu (for this source=%d); need-data requests %zu -> %zu",
           client->cancelCount(), client->sawCancel(sourceId), requestsBefore, client->needDataLog().size());

    // The unanswered request is not a failure, however the server accounts for it.
    UT_ASSERT_FALSE(client->sawPlaybackState(PlaybackState::FAILURE));

    // Resume feeding: the protocol must carry on once the client answers again.
    client->rewindSource(sourceId);
    client->setWithhold(false);
    std::this_thread::sleep_for(kSettleTime);
    UT_LOG("[data-fault] after resuming: need-data requests=%zu accepted segments=%zu",
           client->needDataLog().size(), client->addSegmentOkCount());
    UT_ASSERT_TRUE(client->needDataLog().size() > requestsBefore);
    UT_ASSERT_FALSE(client->sawPlaybackState(PlaybackState::FAILURE));

    pipeline->stop();
    pipeline.reset();
}

/**
 * RC-CORE-DATA-010 — a non-fatal playback error leaves PlaybackState unchanged.
 * The feed corrupts one access unit in every few, so the decoder is handed
 * undecodable data mid-stream while the protocol itself stays well-formed. A
 * dropped frame is not a reason to leave PLAYING.
 */
UT_ADD_TEST(L4DataFaultTests, NonFatalPlaybackErrorLeavesStateUnchanged)
{
    CONFORMANCE_CORE_TEST();

    AacElementaryStream stream = generateAacAdtsStream(kLongFrames);
    UT_ASSERT_TRUE_FATAL(!stream.frames.empty());
    auto client = std::make_shared<FeedingMediaPipelineClient>();
    int32_t sourceId = -1;
    std::unique_ptr<IMediaPipeline> pipeline = driveToPlaying(client, stream, sourceId);
    UT_ASSERT_NOT_NULL_FATAL(pipeline.get());

    // Corrupt from here on, so the run is known to have been playing first.
    client->setCorruptEvery(4);
    std::this_thread::sleep_for(kSettleTime);

    const size_t errors = client->playbackErrorCount();
    UT_LOG("[data-fault] corrupted feed: playback errors=%zu (last=%d, state then=%d); state now=%d", errors,
           static_cast<int>(client->lastPlaybackError()), static_cast<int>(client->stateAtLastPlaybackError()),
           static_cast<int>(client->lastPlaybackState()));

    // Undecodable content is a media fault, not a pipeline failure.
    UT_ASSERT_FALSE(client->sawPlaybackState(PlaybackState::FAILURE));

    if (errors > 0)
    {
        // The clause itself: the state in force when the error arrived is the one
        // playback was already in.
        UT_ASSERT_EQUAL(static_cast<int>(client->stateAtLastPlaybackError()), static_cast<int>(PlaybackState::PLAYING));
    }
    else
    {
        // Whether a dropped frame is announced to the client is decoder-dependent
        // — the software decoder conceals what a vendor one reports. Recorded.
        UT_LOG("[data-fault] no playback error announced for corrupt content on this platform");
    }

    pipeline->stop();
    pipeline.reset();
}
