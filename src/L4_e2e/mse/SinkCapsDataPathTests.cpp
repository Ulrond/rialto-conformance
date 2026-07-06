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
 * @file SinkCapsDataPathTests.cpp
 *
 * L4 — end-to-end data-path case for Surface A: the incoming CAPS-event field parse
 * of RC-CORE-MSECAPS-006. The sink's CAPS handler reads codec_data, alignment,
 * stream-format (and, on H.265, the Dolby-Vision dovi-stream/dv_profile) out of the
 * incoming caps and carries them into the Rialto video source it attaches
 * (rialto-gstreamer GStreamerMSEUtils get_codec_data / get_segment_alignment /
 * get_stream_format, PullModeVideoPlaybackDelegate::createMediaSource). This is an
 * internal data-flow transform, so it is driven — not introspected.
 *
 * The parse is proven field-sensitively: the fed H.264 stream is in **AVC form**
 * (stream-format=avc, alignment=au) with its SPS/PPS out-of-band in codec_data. An
 * AVC stream carries no in-band parameter sets, so the server's decoder cannot
 * configure — and the pipeline cannot preroll to PLAYING — unless the sink parsed
 * codec_data out of the CAPS event and delivered it to the source. Reaching PLAYING
 * is therefore the evidence that codec_data + stream-format + alignment were parsed
 * and applied; a byte-stream H.264 stream (SPS/PPS in-band) could not distinguish
 * this. H.264 is a baseline-required codec, so the case runs unconditionally.
 *
 * The Dolby-Vision (dovi-stream/dv_profile) and raw-audio-layout field groups of
 * RC-CORE-MSECAPS-006 are platform/content-variable: DV parsing routes through the
 * H.265 source branch and needs real DV content (issue #22) on a DV-capable
 * platform, so it is capability-gated (codecs.video.dolbyVision) rather than
 * synthesised here.
 *
 * Coverage trace: coverage/rc-core-catalog.yaml / matrix.yaml — RC-CORE-MSECAPS-006.
 */

#include <ut.h>
#include <ut_log.h>

#include "conformance/MediaFeed.h"
#include "conformance/MseVideoSinkFeed.h"
#include "conformance/Surfaces.h"
#include "conformance/TierGate.h"

#include <chrono>

using rialto::conformance::generateH264AvcStream;
using rialto::conformance::H264AvcElementaryStream;
using rialto::conformance::MseSinkSurface;
using rialto::conformance::MseVideoSinkFeed;

namespace
{
// H.264 frames to synthesise (~1.2s at 25 fps) — enough for the server to reach and
// hold PLAYING while the parse is proven.
constexpr int kH264Frames = 30;

// Upper bound on reaching PLAYING through the MSE video sink on the headless
// software render path (the audio data path uses the same ceiling).
constexpr std::chrono::milliseconds kPlayingTimeout{15000};

class L4SinkCapsDataPathTests : public MseSinkSurface
{
};
} // namespace

UT_ADD_TEST_TO_GROUP(L4SinkCapsDataPathTests, UT_TESTS_L4);

/**
 * RC-CORE-MSECAPS-006 — incoming CAPS-event fields (codec_data, alignment=au,
 * stream-format=avc) are parsed and applied to the attached video source. An
 * AVC-form H.264 stream is fed through rialtomsevideosink; because AVC carries no
 * in-band SPS/PPS, only a sink that parsed codec_data out of the CAPS event (and
 * the stream-format/alignment that select AVC) lets the server's decoder configure
 * and the pipeline reach PLAYING. Reaching PLAYING is the field-sensitive proof.
 */
UT_ADD_TEST(L4SinkCapsDataPathTests, AvcCapsFieldsParsedReachingPlaying)
{
    CONFORMANCE_CORE_TEST();

    H264AvcElementaryStream stream = generateH264AvcStream(kH264Frames);
    UT_LOG("[mse-datapath] synthesised H.264 AVC frames=%zu codec_data=%zuB %ux%u", stream.frames.size(),
           stream.codecData.size(), stream.width, stream.height);
    // The AVC codec_data (avcC SPS/PPS) is the crux: a stream without it cannot prove
    // the parse. H.264 is baseline-required, so a synthesis failure is a platform
    // fault, not a soft skip.
    UT_ASSERT_TRUE_FATAL(stream.isComplete());

    MseVideoSinkFeed feed(stream);
    UT_ASSERT_TRUE_FATAL(feed.isValid());

    const bool reachedPlaying = feed.startPlaying(kPlayingTimeout);
    UT_LOG("[mse-datapath] AVC video feed reached PLAYING: %d", reachedPlaying);
    UT_ASSERT_TRUE(reachedPlaying);
}
