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
 * @file ControlTests.cpp
 *
 * L1 — function testing for Surface B: IControl / IControlClient.
 *
 * IControl is the entry point to the native client API: it owns the IPC
 * connection and shared memory, registers a client for application-state
 * callbacks, and reports the current state. These cases exercise the published
 * IControl contract on its own (§6 L1), driving a live RialtoServer over the
 * IPC socket exactly as an external app does — never Rialto internals.
 *
 * Coverage trace: coverage/matrix.yaml / rc-core-catalog.yaml rows
 * RC-CORE-CONTROL-001 (registerClient returns true + writes the ApplicationState
 * out-param), RC-CORE-CONTROL-002 (a registered client is informed of the live
 * server application state), RC-CORE-CONTROL-003 (the client shared_ptr is
 * released on IControl destruction).
 *
 * Note on the state-notification contract (RC-CORE-CONTROL-002): a client that
 * registers against an already-running server is given the current state
 * synchronously in registerClient's out-param; the notifyApplicationState
 * callback delivers *subsequent* transitions. RegisteredClientLearnsRunningState
 * verifies the former (the client learns RUNNING from the live server);
 * RegisteredClientIsNotifiedOfStateTransition verifies the latter by driving an
 * INACTIVE<->ACTIVE change through the gate harness's ServerManagerSim
 * (SimControl) while the client is registered, and asserting the callback
 * observes each edge. The transition case gates on `control.stateToggle` — only
 * a gate that controls its own sim can provoke the edge, so it self-skips on
 * real hardware.
 */

#include <ut.h>

#include "conformance/CapabilityGate.h"
#include "conformance/SimControl.h"
#include "conformance/Surfaces.h"
#include "conformance/TierGate.h"

#include "IControl.h"
#include "IControlClient.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

using namespace firebolt::rialto;
using rialto::conformance::NativeClientSurface;

namespace
{
/// True iff @p state is one of the defined ApplicationState enumerators — i.e.
/// the out-param was written with a real state value, not left/clobbered.
bool isDefinedState(ApplicationState state)
{
    switch (state)
    {
    case ApplicationState::UNKNOWN:
    case ApplicationState::RUNNING:
    case ApplicationState::INACTIVE:
        return true;
    }
    return false;
}

/**
 * A minimal IControlClient stub. These IControl cases assert the registration,
 * state-reporting and lifetime contracts, none of which require inspecting a
 * delivered callback, so the client only needs to be a valid IControlClient.
 */
class StubControlClient : public IControlClient
{
public:
    void notifyApplicationState(ApplicationState) override {}
};

/**
 * An IControlClient that records every notifyApplicationState callback so a case
 * can assert the client observed a transition the sim drove. Thread-safe: the
 * callback arrives on the IPC client's thread, the test waits on the main thread.
 */
class RecordingControlClient : public IControlClient
{
public:
    void notifyApplicationState(ApplicationState state) override
    {
        {
            std::lock_guard<std::mutex> lock{m_mutex};
            m_states.push_back(state);
        }
        m_cv.notify_all();
    }

    /// Number of callbacks received so far — a marker to wait *after*.
    std::size_t count()
    {
        std::lock_guard<std::mutex> lock{m_mutex};
        return m_states.size();
    }

    /// Wait until a notifyApplicationState(@p state) arrives at or after index
    /// @p since, or @p timeout elapses. Robust to duplicate/extra callbacks.
    bool waitForStateSince(std::size_t since, ApplicationState state, std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock{m_mutex};
        return m_cv.wait_for(lock, timeout,
                             [&]
                             {
                                 for (std::size_t i = since; i < m_states.size(); ++i)
                                 {
                                     if (m_states[i] == state)
                                     {
                                         return true;
                                     }
                                 }
                                 return false;
                             });
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::vector<ApplicationState> m_states;
};

class L1ControlTests : public NativeClientSurface
{
};
} // namespace

UT_ADD_TEST_TO_GROUP(L1ControlTests, UT_TESTS_L1);

/**
 * RC-CORE-CONTROL-001 — IControlFactory yields a concrete IControl, and
 * registerClient returns true and writes the current ApplicationState into its
 * out-param. The synchronous contract: returns true and writes a defined state.
 */
UT_ADD_TEST(L1ControlTests, RegisterClientReturnsTrueAndWritesState)
{
    CONFORMANCE_CORE_TEST();

    auto factory = IControlFactory::createFactory();
    UT_ASSERT_NOT_NULL_FATAL(factory.get());

    auto control = factory->createControl();
    UT_ASSERT_NOT_NULL_FATAL(control.get());

    auto client = std::make_shared<StubControlClient>();
    ApplicationState appState = ApplicationState::UNKNOWN;
    UT_ASSERT_TRUE(control->registerClient(client, appState));
    UT_ASSERT_TRUE(isDefinedState(appState));
}

/**
 * RC-CORE-CONTROL-002 — a registered IControlClient is informed of the server's
 * application state. Against the live RialtoServer (brought up Active by the
 * harness), registering hands the client the current state via the out-param,
 * which must report RUNNING — proving the IPC connection and that the client is
 * told the real server state, not a placeholder.
 */
UT_ADD_TEST(L1ControlTests, RegisteredClientLearnsRunningState)
{
    CONFORMANCE_CORE_TEST();

    auto factory = IControlFactory::createFactory();
    UT_ASSERT_NOT_NULL_FATAL(factory.get());

    auto control = factory->createControl();
    UT_ASSERT_NOT_NULL_FATAL(control.get());

    auto client = std::make_shared<StubControlClient>();
    ApplicationState appState = ApplicationState::UNKNOWN;
    UT_ASSERT_TRUE(control->registerClient(client, appState));

    // The live server is Active, so the registering client must be told RUNNING.
    UT_ASSERT_EQUAL(appState, ApplicationState::RUNNING);
}

/**
 * RC-CORE-CONTROL-002 (notify-on-transition clause) — a registered IControlClient
 * receives notifyApplicationState on a *server* application-state transition. The
 * registerClient out-param only hands over the state at registration; subsequent
 * edges arrive via the callback (ControlServerInternal::setApplicationState). The
 * gate harness activates the server before the client connects, so the client
 * must provoke the edge: it drives the app INACTIVE then back ACTIVE through the
 * ServerManagerSim (SimControl) and asserts the callback observed RUNNING->
 * INACTIVE->RUNNING. Gated on `control.stateToggle`: only a gate controlling its
 * own sim can drive this, so it self-skips on real hardware.
 */
UT_ADD_TEST(L1ControlTests, RegisteredClientIsNotifiedOfStateTransition)
{
    CONFORMANCE_CORE_TEST();
    CONFORMANCE_REQUIRE_CAP("control.stateToggle");

    rialto::conformance::SimControl sim;
    // The capability is declared, so the harness must have exported the sim
    // endpoint; a miss here is a harness misconfiguration, not a soft skip.
    UT_ASSERT_TRUE_FATAL(rialto::conformance::SimControl::fromEnvironment(sim));

    auto factory = IControlFactory::createFactory();
    UT_ASSERT_NOT_NULL_FATAL(factory.get());
    auto control = factory->createControl();
    UT_ASSERT_NOT_NULL_FATAL(control.get());

    auto client = std::make_shared<RecordingControlClient>();
    ApplicationState appState = ApplicationState::UNKNOWN;
    UT_ASSERT_TRUE(control->registerClient(client, appState));
    // Precondition: the live server is Active, so the client starts at RUNNING.
    UT_ASSERT_EQUAL(appState, ApplicationState::RUNNING);

    // Always restore the app to Active on exit so the state the shared server is
    // left in does not leak into later cases (which expect RUNNING), even if an
    // assertion below returns early.
    struct RestoreActive
    {
        rialto::conformance::SimControl &sim;
        ~RestoreActive()
        {
            for (int i = 0; i < 25 && sim.getState() != "Active"; ++i)
            {
                sim.setActive();
                std::this_thread::sleep_for(std::chrono::milliseconds{100});
            }
        }
    } restore{sim};

    constexpr auto kTimeout = std::chrono::seconds{5};

    // RUNNING -> INACTIVE: the client must be notified of the deactivation.
    const std::size_t beforeInactive = client->count();
    UT_ASSERT_TRUE(sim.setInactive());
    UT_ASSERT_TRUE(client->waitForStateSince(beforeInactive, ApplicationState::INACTIVE, kTimeout));

    // INACTIVE -> RUNNING: and of the re-activation.
    const std::size_t beforeActive = client->count();
    UT_ASSERT_TRUE(sim.setActive());
    UT_ASSERT_TRUE(client->waitForStateSince(beforeActive, ApplicationState::RUNNING, kTimeout));
}

/**
 * RC-CORE-CONTROL-003 — IControl holds a shared_ptr to the registered client
 * until its own destruction, then releases it. Registered via a weak_ptr whose
 * only strong owner (after the local handle is dropped) is IControl: the client
 * stays alive while IControl lives and is released when IControl is destroyed.
 */
UT_ADD_TEST(L1ControlTests, ClientReleasedOnControlDestruction)
{
    CONFORMANCE_CORE_TEST();

    auto factory = IControlFactory::createFactory();
    UT_ASSERT_NOT_NULL_FATAL(factory.get());

    auto control = factory->createControl();
    UT_ASSERT_NOT_NULL_FATAL(control.get());

    auto client = std::make_shared<StubControlClient>();
    std::weak_ptr<StubControlClient> weakClient = client;

    ApplicationState appState = ApplicationState::UNKNOWN;
    UT_ASSERT_TRUE(control->registerClient(client, appState));

    // Drop our strong reference: IControl now owns the only one, so the client
    // must remain alive.
    client.reset();
    UT_ASSERT_FALSE(weakClient.expired());

    // Destroying IControl unregisters and releases the held client shared_ptr.
    control.reset();
    UT_ASSERT_TRUE(weakClient.expired());
}
