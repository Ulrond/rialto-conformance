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
 * @file StateTests.cpp
 *
 * L1 — function testing for mseSink interface: the GstElement state-change semantics of
 * RialtoMSEBaseSink. An external media app drives these sinks purely through the
 * GStreamer state machine (gst_element_set_state / the element's change_state
 * vfunc), so the suite exercises the same surface and asserts the documented
 * GstStateChangeReturn for each transition. No Rialto internal headers are
 * involved (§2 mseSink interface); the base-sink behaviour is exercised on the audio sink
 * as a representative concrete instance (as the base-sink property cases are).
 *
 * These cases run against the live RialtoServer the gate stands up (the sink
 * connects over RIALTO_SOCKET_PATH), so the control client can reach RUNNING and
 * the media-player transitions return their real codes.
 *
 * Coverage trace: coverage/rc-core-catalog.yaml — RC-CORE-MSESTATE-001,002,003,
 * 004,005,006.
 */

#include <ut.h>

#include "conformance/Surfaces.h"
#include "conformance/TierGate.h"

#include <gst/gst.h>

using rialto::conformance::MseSinkSurface;

namespace
{
class L1StateTests : public MseSinkSurface
{
};

// A fresh audio sink (the base-sink behaviour under test lives on every sink).
GstElement *makeAudioSink()
{
    GstElement *sink = gst_element_factory_make("rialtomseaudiosink", nullptr);
    UT_ASSERT_NOT_NULL(sink);
    return sink;
}
} // namespace

UT_ADD_TEST_TO_GROUP(L1StateTests, UT_TESTS_L1);

// The MSE sink state machine is part of the required mseSink interface, so these cases
// are tested unconditionally against the running server the gate provides.

/**
 * RC-CORE-MSESTATE-001 — a state change attempted with no playback delegate
 * returns GST_STATE_CHANGE_FAILURE. The delegate is created by the sink on
 * NULL->READY, so a change_state invoked directly (via the element class vfunc)
 * for any other transition on a freshly-made sink hits the no-delegate guard and
 * fails, rather than dereferencing a null delegate.
 */
UT_ADD_TEST(L1StateTests, NullDelegateStateChangeFails)
{
    CONFORMANCE_CORE_TEST();
    GstElement *sink = makeAudioSink();
    UT_ASSERT_NOT_NULL_FATAL(sink);

    GstStateChangeReturn result =
        GST_ELEMENT_GET_CLASS(sink)->change_state(sink, GST_STATE_CHANGE_READY_TO_PAUSED);
    UT_ASSERT_EQUAL(result, GST_STATE_CHANGE_FAILURE);

    gst_object_unref(sink);
}

/**
 * RC-CORE-MSESTATE-002 — NULL->READY has the sink pad it requires and completes
 * once the Rialto control client reaches RUNNING. On a conformant platform (a
 * running server) the transition succeeds; the ALWAYS "sink" pad the transition
 * depends on is present on the created element.
 */
UT_ADD_TEST(L1StateTests, NullToReadyReachesReadyWhenRunning)
{
    CONFORMANCE_CORE_TEST();
    GstElement *sink = makeAudioSink();
    UT_ASSERT_NOT_NULL_FATAL(sink);

    GstPad *pad = gst_element_get_static_pad(sink, "sink");
    UT_ASSERT_NOT_NULL(pad);
    if (pad != nullptr)
        gst_object_unref(pad);

    UT_ASSERT_EQUAL(gst_element_set_state(sink, GST_STATE_READY), GST_STATE_CHANGE_SUCCESS);

    gst_element_set_state(sink, GST_STATE_NULL);
    gst_object_unref(sink);
}

/**
 * RC-CORE-MSESTATE-003 — READY->PAUSED tolerates a source that is not yet
 * attached (its caps arrive later) and returns GST_STATE_CHANGE_ASYNC: the sink
 * asks the server to pause, gets async/not-attached, posts async-start, and waits
 * for the source rather than failing.
 */
UT_ADD_TEST(L1StateTests, ReadyToPausedIsAsyncWhenSourceNotAttached)
{
    CONFORMANCE_CORE_TEST();
    GstElement *sink = makeAudioSink();
    UT_ASSERT_NOT_NULL_FATAL(sink);

    UT_ASSERT_EQUAL(gst_element_set_state(sink, GST_STATE_READY), GST_STATE_CHANGE_SUCCESS);
    UT_ASSERT_EQUAL(gst_element_set_state(sink, GST_STATE_PAUSED), GST_STATE_CHANGE_ASYNC);

    gst_element_set_state(sink, GST_STATE_NULL);
    gst_object_unref(sink);
}

/**
 * RC-CORE-MSESTATE-004 — PAUSED->PLAYING requires an attached source; with a
 * media-player client present but no source attached it returns
 * GST_STATE_CHANGE_FAILURE. READY->PAUSED creates the sink's media-player client
 * and returns ASYNC without attaching a source (the source attaches later, when
 * GST_EVENT_CAPS arrives — see MSESTATE-003), so pushing no caps leaves the
 * client's attached-source set empty. Invoking the PAUSED->PLAYING transition
 * directly (the element-class vfunc, as MSESTATE-001 does) then drives
 * client->play() with an unknown source id: it reports NOT_ATTACHED, which the
 * sink maps to GST_STATE_CHANGE_FAILURE rather than a spurious async play. This is
 * a provoked negative — a real media app attaches the source (via caps) before
 * requesting PLAYING, so the not-attached play is never reached in the happy path.
 */
UT_ADD_TEST(L1StateTests, PausedToPlayingFailsWhenSourceNotAttached)
{
    CONFORMANCE_CORE_TEST();
    GstElement *sink = makeAudioSink();
    UT_ASSERT_NOT_NULL_FATAL(sink);

    // READY then PAUSED: the control client reaches RUNNING and the media-player
    // client is created; PAUSED is async because no source is attached yet.
    UT_ASSERT_EQUAL(gst_element_set_state(sink, GST_STATE_READY), GST_STATE_CHANGE_SUCCESS);
    UT_ASSERT_EQUAL(gst_element_set_state(sink, GST_STATE_PAUSED), GST_STATE_CHANGE_ASYNC);

    // With a client but no attached source, PAUSED->PLAYING fails (NOT_ATTACHED).
    GstStateChangeReturn result =
        GST_ELEMENT_GET_CLASS(sink)->change_state(sink, GST_STATE_CHANGE_PAUSED_TO_PLAYING);
    UT_ASSERT_EQUAL(result, GST_STATE_CHANGE_FAILURE);

    gst_element_set_state(sink, GST_STATE_NULL);
    gst_object_unref(sink);
}

/**
 * RC-CORE-MSESTATE-005 — PAUSED->READY tears the source down: it removes the
 * server source, clears any queued buffers, and (because the pending READY->PAUSED
 * left a commit outstanding) posts async-done. The downward transition completes
 * synchronously (GST_STATE_CHANGE_SUCCESS).
 */
UT_ADD_TEST(L1StateTests, PausedToReadyTearsDownAndSucceeds)
{
    CONFORMANCE_CORE_TEST();
    GstElement *sink = makeAudioSink();
    UT_ASSERT_NOT_NULL_FATAL(sink);

    UT_ASSERT_EQUAL(gst_element_set_state(sink, GST_STATE_READY), GST_STATE_CHANGE_SUCCESS);
    UT_ASSERT_EQUAL(gst_element_set_state(sink, GST_STATE_PAUSED), GST_STATE_CHANGE_ASYNC);
    UT_ASSERT_EQUAL(gst_element_set_state(sink, GST_STATE_READY), GST_STATE_CHANGE_SUCCESS);

    gst_element_set_state(sink, GST_STATE_NULL);
    gst_object_unref(sink);
}

/**
 * RC-CORE-MSESTATE-006 — READY->NULL releases the media-player client and removes
 * the control backend, completing synchronously (GST_STATE_CHANGE_SUCCESS). After
 * it, the delegate is gone, so the sink can be brought back up from NULL again.
 */
UT_ADD_TEST(L1StateTests, ReadyToNullReleasesAndSucceeds)
{
    CONFORMANCE_CORE_TEST();
    GstElement *sink = makeAudioSink();
    UT_ASSERT_NOT_NULL_FATAL(sink);

    UT_ASSERT_EQUAL(gst_element_set_state(sink, GST_STATE_READY), GST_STATE_CHANGE_SUCCESS);
    UT_ASSERT_EQUAL(gst_element_set_state(sink, GST_STATE_NULL), GST_STATE_CHANGE_SUCCESS);

    // The release is clean: a second NULL->READY brings a fresh delegate up.
    UT_ASSERT_EQUAL(gst_element_set_state(sink, GST_STATE_READY), GST_STATE_CHANGE_SUCCESS);
    gst_element_set_state(sink, GST_STATE_NULL);

    gst_object_unref(sink);
}
