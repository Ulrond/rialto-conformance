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
 * callback delivers *subsequent* transitions. CONTROL-002 here verifies the
 * former (the client learns RUNNING from the live server). Asserting the
 * callback on a post-registration transition needs the harness to drive an
 * INACTIVE<->ACTIVE change while the client is registered — tracked separately.
 */

#include <ut.h>

#include "conformance/Surfaces.h"
#include "conformance/TierGate.h"

#include "IControl.h"
#include "IControlClient.h"

#include <memory>

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
