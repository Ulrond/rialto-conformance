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
 * @file Surfaces.cpp
 *
 * Shared setup/teardown for the two external-surface fixtures (§2). Phase 0
 * provides the lifecycle hooks the L1+ cases build on; the per-level cases fill
 * in the surface drives. Nothing here touches Rialto internals.
 */

#include "conformance/Surfaces.h"

#include <gst/gst.h>

namespace rialto::conformance
{
void MseSinkSurface::SetUp()
{
    // Initialise GStreamer once per process; the app owns the pipeline and
    // plugs the rialtomse*sink elements in by registry name (Surface A).
    if (!gst_is_initialized())
    {
        gst_init(nullptr, nullptr);
    }
}

void MseSinkSurface::TearDown()
{
    // Per-case pipelines are owned by the individual cases; nothing global to
    // release here.
}

void NativeClientSurface::SetUp()
{
    // L1 function cases drive the public factories directly; module/group/E2E
    // cases extend this to create IControl, register a client, and reach
    // RUNNING before exercising IMediaPipeline / IMediaKeys / IWebAudioPlayer.
}

void NativeClientSurface::TearDown()
{
    // Release client objects in reverse order of creation; per-case fixtures
    // own the concrete handles.
}

} // namespace rialto::conformance
