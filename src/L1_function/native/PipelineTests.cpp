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
 * @file PipelineTests.cpp
 *
 * L1 — function testing for Firebolt interface: IMediaPipeline (§6 L1).
 *
 * IMediaPipeline is the native client API an external media app uses to drive
 * playback: it creates a pipeline bound to a client through the published
 * factory, loads/attaches sources, transports (play/pause/seek) and queries the
 * pipeline. These cases exercise the create + synchronous introspection surface
 * of that published contract against the live RialtoServer the gate stands up
 * (the client connects over RIALTO_SOCKET_PATH), never Rialto internals.
 *
 * This batch is the create + client-binding introspection subset that runs
 * unconditionally on the software platform: creating the pipeline and reading
 * back the bound client are serviced without attaching a source or reaching
 * PLAYING. The rest of the IMediaPipeline surface — including the position /
 * duration / buffering-property getters — only returns a value once a source is
 * attached and the backend media pipeline is realized, so it is covered with the
 * data-path work (tracked as `planned` matrix rows), not here.
 *
 * Coverage trace: coverage/rc-core-catalog.yaml / matrix.yaml rows
 * RC-CORE-PIPE-001 (createMediaPipeline returns a usable instance bound to the
 * client), RC-CORE-PIPE-002 (getClient returns the client supplied at creation).
 */

#include <ut.h>

#include "conformance/Surfaces.h"
#include "conformance/TierGate.h"

#include "IMediaPipeline.h"
#include "IMediaPipelineClient.h"

#include <cstdint>
#include <memory>

using namespace firebolt::rialto;
using rialto::conformance::NativeClientSurface;

namespace
{
// A representative decode ceiling: 1080p. VideoRequirements only bounds the
// frames the backend must be able to decode; the software backend accepts it.
constexpr uint32_t kMaxWidth = 1920;
constexpr uint32_t kMaxHeight = 1080;

/**
 * A do-nothing IMediaPipelineClient. The introspection cases assert synchronous
 * return values and out-params, none of which depend on an asynchronous
 * callback, so the client only needs to be a valid IMediaPipelineClient that the
 * pipeline can bind to and hand back via getClient().
 */
class StubMediaPipelineClient : public IMediaPipelineClient
{
public:
    void notifyDuration(int64_t) override {}
    void notifyPosition(int64_t) override {}
    void notifyNativeSize(uint32_t, uint32_t, double) override {}
    void notifyNetworkState(NetworkState) override {}
    void notifyPlaybackState(PlaybackState) override {}
    void notifyVideoData(bool) override {}
    void notifyAudioData(bool) override {}
    void notifyNeedMediaData(int32_t, size_t, uint32_t, const std::shared_ptr<MediaPlayerShmInfo> &) override {}
    void notifyCancelNeedMediaData(int32_t) override {}
    void notifyQos(int32_t, const QosInfo &) override {}
    void notifyBufferUnderflow(int32_t) override {}
    void notifyFirstFrameReceived(int32_t) override {}
    void notifyPlaybackError(int32_t, PlaybackError) override {}
    void notifySourceFlushed(int32_t) override {}
    void notifyPlaybackInfo(const PlaybackInfo &) override {}
};

class L1PipelineTests : public NativeClientSurface
{
protected:
    /// Create a media pipeline bound to @p client with a 1080p decode ceiling.
    /// The client is owned by the fixture so it outlives the pipeline (the
    /// factory takes a weak_ptr).
    std::unique_ptr<IMediaPipeline> makePipeline(std::shared_ptr<IMediaPipelineClient> client)
    {
        auto factory = IMediaPipelineFactory::createFactory();
        if (!factory)
            return nullptr;
        VideoRequirements requirements{kMaxWidth, kMaxHeight};
        return factory->createMediaPipeline(client, requirements);
    }
};
} // namespace

UT_ADD_TEST_TO_GROUP(L1PipelineTests, UT_TESTS_L1);

/**
 * RC-CORE-PIPE-001 — createMediaPipeline(client, VideoRequirements) returns a
 * usable instance. The create round-trip proves the whole native media-pipeline
 * path links and reaches the live server; a non-null pipeline is the contract.
 */
UT_ADD_TEST(L1PipelineTests, CreateReturnsInstance)
{
    CONFORMANCE_CORE_TEST();

    auto client = std::make_shared<StubMediaPipelineClient>();
    auto pipeline = makePipeline(client);
    UT_ASSERT_NOT_NULL_FATAL(pipeline.get());
}

/**
 * RC-CORE-PIPE-002 — getClient returns the client supplied at creation. The
 * pipeline holds a weak_ptr to the client; locking it must yield the same object
 * the app passed to createMediaPipeline.
 */
UT_ADD_TEST(L1PipelineTests, GetClientReturnsSuppliedClient)
{
    CONFORMANCE_CORE_TEST();

    auto client = std::make_shared<StubMediaPipelineClient>();
    auto pipeline = makePipeline(client);
    UT_ASSERT_NOT_NULL_FATAL(pipeline.get());

    auto returned = pipeline->getClient().lock();
    UT_ASSERT_TRUE(returned.get() == client.get());
}
