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
 * @file WebAudioTests.cpp
 *
 * L1 — function testing for Surface B: IWebAudioPlayer (§6 L1).
 *
 * IWebAudioPlayer is the native client API for mixing PCM audio with the current
 * audio output. An external app creates a player through the published factory,
 * drives it (play / pause / setEos), feeds frames (getBufferAvailable +
 * writeBuffer) and queries it (getBufferDelay / getDeviceInfo / getVolume). These
 * cases exercise that published contract against the live RialtoServer the gate
 * stands up (the client connects over RIALTO_SOCKET_PATH), never Rialto internals.
 *
 * The player create + all synchronous queries run on the software platform: the
 * software backend accepts a single web-audio player and services the query
 * surface without rendering. The play-out state machine (WEBAUDIO-002) drives the
 * backend pipeline to PLAYING, which requires a real audio-render device the
 * headless software platform does not provide; it therefore gates on the
 * `webaudio.playout` platform feature and runs where a target declares it.
 *
 * Coverage trace: coverage/rc-core-catalog.yaml / matrix.yaml rows
 * RC-CORE-WEBAUDIO-001 (create returns an instance in IDLE),
 * RC-CORE-WEBAUDIO-002 (play/pause/setEos state machine + notifyState),
 * RC-CORE-WEBAUDIO-003 (getBufferAvailable bounds the write),
 * RC-CORE-WEBAUDIO-004 (getBufferDelay), RC-CORE-WEBAUDIO-005 (getDeviceInfo),
 * RC-CORE-WEBAUDIO-006 (setVolume/getVolume round-trip),
 * RC-CORE-WEBAUDIO-007 (priority above the supported count still appears to work).
 */

#include <ut.h>

#include "conformance/CapabilityGate.h"
#include "conformance/Surfaces.h"
#include "conformance/TierGate.h"

#include "IWebAudioPlayer.h"

#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

using namespace firebolt::rialto;
using rialto::conformance::NativeClientSurface;

namespace
{
// PCM the software backend accepts: 48 kHz, stereo, signed 16-bit LE integer.
// bytesPerFrame = channels * sampleSize/8 = 2 * 2 = 4.
constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kChannels = 2;
constexpr uint32_t kSampleSizeBits = 16;
constexpr const char *kPcmMimeType = "audio/x-raw";

// The most-important player: priority 1 is always within any platform's count.
constexpr uint32_t kTopPriority = 1;

// notifyState is delivered on the IPC callback thread; a transition must arrive
// within a bounded wait or the case fails rather than blocking the gate.
constexpr auto kStateTimeout = std::chrono::seconds(5);

/**
 * An IWebAudioPlayerClient that records the states the server notifies and lets a
 * case block until an expected state arrives (or a bounded timeout elapses). This
 * is exactly how an external app observes the player's lifecycle.
 */
class RecordingWebAudioClient : public IWebAudioPlayerClient
{
public:
    void notifyState(WebAudioPlayerState state) override
    {
        std::lock_guard<std::mutex> lock{m_mutex};
        m_states.push_back(state);
        m_cv.notify_all();
    }

    /// Block until @p want has been notified at least once, or the timeout elapses.
    bool waitForState(WebAudioPlayerState want)
    {
        std::unique_lock<std::mutex> lock{m_mutex};
        return m_cv.wait_for(lock, kStateTimeout, [&] { return sawLocked(want); });
    }

    bool saw(WebAudioPlayerState want)
    {
        std::lock_guard<std::mutex> lock{m_mutex};
        return sawLocked(want);
    }

private:
    bool sawLocked(WebAudioPlayerState want) const
    {
        for (WebAudioPlayerState s : m_states)
            if (s == want)
                return true;
        return false;
    }

    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::vector<WebAudioPlayerState> m_states;
};

/// A PCM WebAudioConfig kept alive for the player's lifetime (the factory takes a
/// weak_ptr to it).
std::shared_ptr<WebAudioConfig> makePcmConfig()
{
    auto config = std::make_shared<WebAudioConfig>();
    config->pcm.rate = kSampleRate;
    config->pcm.channels = kChannels;
    config->pcm.sampleSize = kSampleSizeBits;
    config->pcm.isBigEndian = false;
    config->pcm.isSigned = true;
    config->pcm.isFloat = false;
    return config;
}

class L1WebAudioTests : public NativeClientSurface
{
protected:
    /// Create a web-audio player at @p priority, wiring @p client + a fresh PCM
    /// config. The config is owned by the fixture so it outlives the player.
    std::unique_ptr<IWebAudioPlayer> makePlayer(std::shared_ptr<IWebAudioPlayerClient> client, uint32_t priority)
    {
        auto factory = IWebAudioPlayerFactory::createFactory();
        if (!factory)
            return nullptr;
        m_config = makePcmConfig();
        return factory->createWebAudioPlayer(client, kPcmMimeType, priority, m_config);
    }

    std::shared_ptr<WebAudioConfig> m_config;
};
} // namespace

UT_ADD_TEST_TO_GROUP(L1WebAudioTests, UT_TESTS_L1);

/**
 * RC-CORE-WEBAUDIO-001 — createWebAudioPlayer("audio/x-raw", priority, pcm config)
 * returns a usable instance. The player is created in IDLE; the create round-trip
 * proves the whole native web-audio path links and reaches the live server, and
 * the backend has not spontaneously reported a failure state.
 */
UT_ADD_TEST(L1WebAudioTests, CreateReturnsInstance)
{
    CONFORMANCE_CORE_TEST();

    auto client = std::make_shared<RecordingWebAudioClient>();
    auto player = makePlayer(client, kTopPriority);
    UT_ASSERT_NOT_NULL_FATAL(player.get());

    // Created IDLE: the backend must not have driven the player to a failure
    // state on creation.
    UT_ASSERT_FALSE(client->saw(WebAudioPlayerState::FAILURE));
}

/**
 * RC-CORE-WEBAUDIO-002 — play / pause / setEos drive IDLE -> PLAYING / PAUSED ->
 * END_OF_STREAM and the client is notified of each transition. Driving the player
 * to PLAYING renders through the backend audio device, so this gates on the
 * platform declaring web-audio play-out.
 */
UT_ADD_TEST(L1WebAudioTests, StateMachineNotifiesTransitions)
{
    CONFORMANCE_CORE_TEST();
    CONFORMANCE_REQUIRE_CAP("webaudio.playout");

    auto client = std::make_shared<RecordingWebAudioClient>();
    auto player = makePlayer(client, kTopPriority);
    UT_ASSERT_NOT_NULL_FATAL(player.get());

    UT_ASSERT_TRUE(player->play());
    UT_ASSERT_TRUE(client->waitForState(WebAudioPlayerState::PLAYING));

    UT_ASSERT_TRUE(player->pause());
    UT_ASSERT_TRUE(client->waitForState(WebAudioPlayerState::PAUSED));

    UT_ASSERT_TRUE(player->play());
    UT_ASSERT_TRUE(player->setEos());
    UT_ASSERT_TRUE(client->waitForState(WebAudioPlayerState::END_OF_STREAM));
}

/**
 * RC-CORE-WEBAUDIO-003 — getBufferAvailable reports the frames the client may
 * write, bounded by the shared-memory capacity (getDeviceInfo's maximumFrames);
 * a writeBuffer at that bound is accepted. The contract the case proves: the
 * client learns the write bound and must not exceed it.
 */
UT_ADD_TEST(L1WebAudioTests, GetBufferAvailableBoundsTheWrite)
{
    CONFORMANCE_CORE_TEST();

    auto client = std::make_shared<RecordingWebAudioClient>();
    auto player = makePlayer(client, kTopPriority);
    UT_ASSERT_NOT_NULL_FATAL(player.get());

    uint32_t preferredFrames = 0, maximumFrames = 0;
    bool supportDeferredPlay = false;
    UT_ASSERT_TRUE(player->getDeviceInfo(preferredFrames, maximumFrames, supportDeferredPlay));

    uint32_t availableFrames = 0;
    std::shared_ptr<WebAudioShmInfo> shmInfo;
    UT_ASSERT_TRUE(player->getBufferAvailable(availableFrames, shmInfo));

    // The write bound is non-zero and never exceeds the shared-memory capacity.
    UT_ASSERT_TRUE(availableFrames > 0);
    UT_ASSERT_TRUE(availableFrames <= maximumFrames);

    // Writing exactly up to the reported bound is accepted (a matching
    // getBufferAvailable/writeBuffer pair). The data payload is ignored by the
    // backend — the frames are the contract.
    std::vector<uint8_t> frames(static_cast<size_t>(availableFrames) * (kChannels * (kSampleSizeBits / 8)), 0);
    UT_ASSERT_TRUE(player->writeBuffer(availableFrames, frames.data()));
}

/**
 * RC-CORE-WEBAUDIO-004 — getBufferDelay writes the number of frames still to be
 * played. On a freshly created player nothing is queued, so the reported delay is
 * defined (zero) and the call succeeds.
 */
UT_ADD_TEST(L1WebAudioTests, GetBufferDelayReportsQueuedFrames)
{
    CONFORMANCE_CORE_TEST();

    auto client = std::make_shared<RecordingWebAudioClient>();
    auto player = makePlayer(client, kTopPriority);
    UT_ASSERT_NOT_NULL_FATAL(player.get());

    uint32_t delayFrames = UINT32_MAX;
    UT_ASSERT_TRUE(player->getBufferDelay(delayFrames));
    // Nothing has been written, so no frames are queued for play-out.
    UT_ASSERT_EQUAL(delayFrames, 0u);
}

/**
 * RC-CORE-WEBAUDIO-005 — getDeviceInfo writes the preferred and maximum commit
 * sizes and whether deferred play (committing frames before play) is supported.
 * preferred must not exceed maximum, and maximum must be a real capacity.
 */
UT_ADD_TEST(L1WebAudioTests, GetDeviceInfoReportsBufferSizing)
{
    CONFORMANCE_CORE_TEST();

    auto client = std::make_shared<RecordingWebAudioClient>();
    auto player = makePlayer(client, kTopPriority);
    UT_ASSERT_NOT_NULL_FATAL(player.get());

    uint32_t preferredFrames = 0, maximumFrames = 0;
    bool supportDeferredPlay = false;
    UT_ASSERT_TRUE(player->getDeviceInfo(preferredFrames, maximumFrames, supportDeferredPlay));

    UT_ASSERT_TRUE(maximumFrames > 0);
    UT_ASSERT_TRUE(preferredFrames > 0);
    UT_ASSERT_TRUE(preferredFrames <= maximumFrames);
}

/**
 * RC-CORE-WEBAUDIO-006 — setVolume / getVolume round-trip a level in [0.0, 1.0].
 * The volume is a pipeline property serviced without play-out, so this runs on
 * the software platform.
 */
UT_ADD_TEST(L1WebAudioTests, VolumeRoundTrips)
{
    CONFORMANCE_CORE_TEST();

    auto client = std::make_shared<RecordingWebAudioClient>();
    auto player = makePlayer(client, kTopPriority);
    UT_ASSERT_NOT_NULL_FATAL(player.get());

    for (double target : {0.0, 0.5, 1.0})
    {
        UT_ASSERT_TRUE(player->setVolume(target));
        double reported = -1.0;
        UT_ASSERT_TRUE(player->getVolume(reported));
        UT_ASSERT_DOUBLE_EQUAL(reported, target, 0.01);
    }
}

/**
 * RC-CORE-WEBAUDIO-007 — a player created with a priority greater than the
 * platform's supported player count still creates and appears to function; its
 * audio is silently discarded (a documented quiet-fail). The suite asserts the
 * documented behaviour — the player is usable, no error is surfaced — not the
 * unobservable discard.
 */
UT_ADD_TEST(L1WebAudioTests, HighPriorityPlayerAppearsToFunction)
{
    CONFORMANCE_CORE_TEST();

    // A priority well beyond any real supported-player count.
    constexpr uint32_t kBeyondSupportedCount = 100;

    auto client = std::make_shared<RecordingWebAudioClient>();
    auto player = makePlayer(client, kBeyondSupportedCount);
    UT_ASSERT_NOT_NULL_FATAL(player.get());

    // It appears to function: the query surface works and no failure is surfaced.
    uint32_t preferredFrames = 0, maximumFrames = 0;
    bool supportDeferredPlay = false;
    UT_ASSERT_TRUE(player->getDeviceInfo(preferredFrames, maximumFrames, supportDeferredPlay));
    UT_ASSERT_FALSE(client->saw(WebAudioPlayerState::FAILURE));
}

/**
 * RC-CORE-WEBAUDIO-008 — getClient returns the client the player was created with.
 * The web-audio player holds its client weakly (the mirror of the pipeline's
 * getClient contract), so a client the caller still owns is retrievable through
 * the player.
 */
UT_ADD_TEST(L1WebAudioTests, GetClientReturnsSuppliedClient)
{
    CONFORMANCE_CORE_TEST();

    auto client = std::make_shared<RecordingWebAudioClient>();
    auto player = makePlayer(client, kTopPriority);
    UT_ASSERT_NOT_NULL_FATAL(player.get());

    auto returned = player->getClient().lock();
    UT_ASSERT_TRUE(returned.get() == client.get());
}
