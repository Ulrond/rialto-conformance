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

#ifndef RIALTO_CONFORMANCE_SURFACES_H_
#define RIALTO_CONFORMANCE_SURFACES_H_

/**
 * @file Surfaces.h
 *
 * Test fixtures for the two — and only two — northbound external surfaces (§2).
 * Cases derive from these; nothing here reaches into Rialto internals.
 *
 *   Surface A — MSE GStreamer sink: the app keeps its own pipeline and plugs in
 *               the rialtomse{audio,video}sink + text-track elements present on
 *               the target. Under test: element names, properties, caps.
 *   Surface B — Native client API: drive playback through Rialto's published
 *               C++ interfaces (the media/public/include headers) and callbacks.
 */

#include <ut.h>

namespace rialto::conformance
{
/**
 * Base fixture for Surface A (MSE sink) cases.
 *
 * Brings up a minimal GStreamer pipeline owned by the test (as an external
 * media app would), into which the rialtomse*sink elements are plugged by name. No
 * Rialto internal headers are included — the sinks are obtained from the
 * GStreamer registry exactly as an external app sees them.
 */
class MseSinkSurface : public UTCore
{
protected:
    void SetUp() override;    ///< gst_init + locate rialtomse*sink in the registry
    void TearDown() override; ///< tear the pipeline down cleanly

    /// rialtomse*sink element factory names under test (Surface A).
    static constexpr const char *kAudioSink = "rialtomseaudiosink";
    static constexpr const char *kVideoSink = "rialtomsevideosink";
};

/**
 * Base fixture for Surface B (native client API) cases.
 *
 * Owns the IControl handle + the application-state lifecycle so module/group/
 * E2E cases can drive load/attach/play/pause/seek/EOS through the public
 * interfaces and observe the documented client callbacks.
 */
class NativeClientSurface : public UTCore
{
protected:
    void SetUp() override;    ///< create IControl, register client, reach RUNNING
    void TearDown() override; ///< stop + release the client objects
};

} // namespace rialto::conformance

#endif // RIALTO_CONFORMANCE_SURFACES_H_
