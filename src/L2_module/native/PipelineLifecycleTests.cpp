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
 * @file PipelineLifecycleTests.cpp
 *
 * L2 — module-integration cases for the Firebolt interface (native IMediaPipeline)
 * pre-realization lifecycle: load, attachSource, removeSource, and the
 * allSourcesAttached gate. These are the module-setup and state-machine steps
 * that take effect *before* the backend media pipeline is realized — they need
 * neither a fed elementary stream nor a PLAYING pipeline, so they are asserted
 * here without a data path.
 *
 * That data-path independence is the point: a fault in load / attach / the
 * allSourcesAttached guard is localised at this level, deterministically and
 * without the software render path (issue #18) the L4 data-path cases require.
 * The transitions that only take effect once the pipeline is realized — play,
 * pause, stop, setPlaybackRate, and setPosition's seek branch — stay at L4
 * (PipelineTransportDataPathTests), where a real source is fed to PLAYING.
 *
 * Coverage trace: coverage/rc-core-catalog.yaml / matrix.yaml —
 * RC-CORE-PIPE-003 (load), -004 (attachSource assigns a retrievable id, incl.
 * the multi-source composition leg), -005 (removeSource), -006
 * (allSourcesAttached legal only once, gating on the whole attached set).
 */

#include <ut.h>
#include <ut_log.h>

#include "conformance/Surfaces.h"
#include "conformance/TierGate.h"

#include "IMediaPipeline.h"
#include "IMediaPipelineClient.h"
#include "MediaCommon.h"

#include <cstddef>
#include <memory>

using namespace firebolt::rialto;
using rialto::conformance::NativeClientSurface;

namespace
{
// A nominal decode ceiling; VideoRequirements is always supplied at creation
// even for an audio-only session.
constexpr uint32_t kMaxWidth = 1920;
constexpr uint32_t kMaxHeight = 1080;

// Nominal audio-source configuration. No stream is fed, so the values only have
// to be well-formed for attachSource — the bytes never reach a decoder.
constexpr uint32_t kNominalChannels = 2;
constexpr uint32_t kNominalSampleRate = 48000;

// Nominal video geometry for the composition case's video source (attach-only).
constexpr int32_t kNominalVideoWidth = 640;
constexpr int32_t kNominalVideoHeight = 360;

/**
 * Create a pipeline bound to @p client and load MSE. Leaves attachSource to the
 * caller so each case drives the module step it asserts. Returns nullptr if any
 * setup step fails (the caller fatal-asserts the result).
 */
std::unique_ptr<IMediaPipeline> createAndLoad(const std::shared_ptr<IMediaPipelineClient> &client)
{
    auto factory = IMediaPipelineFactory::createFactory();
    if (!factory)
        return nullptr;

    VideoRequirements requirements{kMaxWidth, kMaxHeight};
    std::unique_ptr<IMediaPipeline> pipeline = factory->createMediaPipeline(client, requirements);
    if (!pipeline)
        return nullptr;

    if (!pipeline->load(MediaType::MSE, "", "mse://1", false))
        return nullptr;
    return pipeline;
}

/// Build a well-formed audio MediaSource (no DRM) with the nominal config.
std::unique_ptr<IMediaPipeline::MediaSource> makeAudioSource()
{
    AudioConfig audioConfig;
    audioConfig.numberOfChannels = kNominalChannels;
    audioConfig.sampleRate = kNominalSampleRate;
    return std::make_unique<IMediaPipeline::MediaSourceAudio>("audio/mp4", false, audioConfig);
}

/// A minimal IMediaPipelineClient for the pre-realization lifecycle: the cases
/// never play, so no need-data callback ever fires and the client only has to
/// satisfy createMediaPipeline's non-null client contract.
class InertPipelineClient : public IMediaPipelineClient
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

class L2PipelineLifecycleTests : public NativeClientSurface
{
};
} // namespace

UT_ADD_TEST_TO_GROUP(L2PipelineLifecycleTests, UT_TESTS_L2);

/**
 * RC-CORE-PIPE-003 + RC-CORE-PIPE-004 + RC-CORE-PIPE-005 — the module-setup
 * round-trip. load(MSE, ...) returns true for a supported source type;
 * attachSource is valid for MediaType::MSE and assigns a source id retrievable
 * via MediaSource::getId() (the handle every later transport/data call uses);
 * removeSource(id) returns true for that previously attached id. None of these
 * needs a realized backend, so the case never feeds data or plays.
 */
UT_ADD_TEST(L2PipelineLifecycleTests, LoadAttachRemoveSource)
{
    CONFORMANCE_CORE_TEST();

    auto client = std::make_shared<InertPipelineClient>();
    std::unique_ptr<IMediaPipeline> pipeline = createAndLoad(client);
    UT_ASSERT_NOT_NULL_FATAL(pipeline.get()); // PIPE-003: load succeeded

    // PIPE-004: attachSource assigns a retrievable id.
    std::unique_ptr<IMediaPipeline::MediaSource> source = makeAudioSource();
    UT_ASSERT_TRUE(pipeline->attachSource(source));
    const int32_t sourceId = source->getId();
    UT_LOG("[lifecycle] attached audio source id=%d", sourceId);
    UT_ASSERT_TRUE(sourceId >= 0);

    // PIPE-005: removeSource succeeds for the attached id.
    UT_ASSERT_TRUE(pipeline->removeSource(sourceId));

    pipeline.reset();
}

/**
 * RC-CORE-PIPE-006 — allSourcesAttached must be called before streaming and may
 * be called only once: the first call returns true, a second call is illegal and
 * returns false (the server guards it with m_wasAllSourcesAttachedCalled). A pure
 * state-machine guard with no data path — the tightest localiser in the suite for
 * this transition.
 */
UT_ADD_TEST(L2PipelineLifecycleTests, AllSourcesAttachedIsLegalOnlyOnce)
{
    CONFORMANCE_CORE_TEST();

    auto client = std::make_shared<InertPipelineClient>();
    std::unique_ptr<IMediaPipeline> pipeline = createAndLoad(client);
    UT_ASSERT_NOT_NULL_FATAL(pipeline.get());

    std::unique_ptr<IMediaPipeline::MediaSource> source = makeAudioSource();
    UT_ASSERT_TRUE(pipeline->attachSource(source));

    UT_ASSERT_TRUE(pipeline->allSourcesAttached());
    // A second call is illegal and must be rejected.
    UT_ASSERT_FALSE(pipeline->allSourcesAttached());

    pipeline.reset();
}

/**
 * RC-CORE-PIPE-004 (composition leg) + RC-CORE-PIPE-006 — the module composed of
 * more than one source. Attaching an audio and a video source assigns each a
 * distinct retrievable id, and the single allSourcesAttached gate covers the
 * whole attached set (not per source). This is the multi-source composition the
 * single-source transport cases do not exercise.
 *
 * Attach-only by design: the case asserts the attach/compose contract and stops
 * before the realized-backend boundary, so it needs no video decode/render path.
 * A full audio+video feed to PLAYING is a distinct L4 concern (the native VIDEO
 * feed, PIPE-016, planned) and is out of scope here.
 */
UT_ADD_TEST(L2PipelineLifecycleTests, MultiSourceAttachComposition)
{
    CONFORMANCE_CORE_TEST();

    auto client = std::make_shared<InertPipelineClient>();
    std::unique_ptr<IMediaPipeline> pipeline = createAndLoad(client);
    UT_ASSERT_NOT_NULL_FATAL(pipeline.get());

    // PIPE-004: audio source gets a retrievable id.
    std::unique_ptr<IMediaPipeline::MediaSource> audioSource = makeAudioSource();
    UT_ASSERT_TRUE(pipeline->attachSource(audioSource));
    const int32_t audioId = audioSource->getId();
    UT_ASSERT_TRUE(audioId >= 0);

    // PIPE-004 (composition): video source gets its own, distinct id.
    std::unique_ptr<IMediaPipeline::MediaSource> videoSource =
        std::make_unique<IMediaPipeline::MediaSourceVideo>("video/h264", false, kNominalVideoWidth, kNominalVideoHeight);
    UT_ASSERT_TRUE(pipeline->attachSource(videoSource));
    const int32_t videoId = videoSource->getId();
    UT_ASSERT_TRUE(videoId >= 0);

    UT_LOG("[lifecycle] composed sources audioId=%d videoId=%d", audioId, videoId);
    UT_ASSERT_TRUE(audioId != videoId);

    // PIPE-006: one allSourcesAttached gates the whole set (both sources in).
    UT_ASSERT_TRUE(pipeline->allSourcesAttached());
    UT_ASSERT_FALSE(pipeline->allSourcesAttached());

    pipeline.reset();
}
