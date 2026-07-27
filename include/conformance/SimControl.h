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

#ifndef RIALTO_CONFORMANCE_SIM_CONTROL_H_
#define RIALTO_CONFORMANCE_SIM_CONTROL_H_

/**
 * @file SimControl.h
 *
 * Test-side handle to the RialtoServerManagerSim HTTP control surface, so a case
 * can drive the server's application-state machine *after* it has connected.
 *
 * The native client API (Firebolt interface) reports the server application state to a
 * registering IControl client via the registerClient out-param, but the
 * notifyApplicationState callback only fires on a subsequent *transition*
 * (ControlServerInternal::setApplicationState). The gate harness activates the
 * server before the conformance binary connects, so a client that registers
 * afterwards never observes an edge on its own. Covering RC-CORE-CONTROL-002's
 * callback clause therefore needs the test to provoke a transition while
 * registered — which is exactly what the sim's `SetState/<app>/<state>` endpoint
 * does (the same one docker/run-in-container.sh uses to activate the app).
 *
 * This is a software-platform harness capability, not a Rialto interface: it is
 * available only where the gate runs against a ServerManagerSim it controls, so
 * cases gate on the `control.stateToggle` deviceConfig feature and read the sim
 * endpoint the harness exports (RIALTO_CONFORMANCE_SIM_HOST/PORT +
 * RIALTO_CONFORMANCE_APP). On real hardware neither is present and the case
 * self-skips.
 */

#include <string>

namespace rialto::conformance
{
/**
 * Drives one app's session-server state through the ServerManagerSim HTTP surface
 * the gate harness stands up. Endpoint + app name come from the environment the
 * harness exports; construct with fromEnvironment().
 */
class SimControl
{
public:
    /**
     * @brief Build a SimControl from the harness-exported environment, if present.
     *
     * Reads RIALTO_CONFORMANCE_SIM_HOST, RIALTO_CONFORMANCE_SIM_PORT and
     * RIALTO_CONFORMANCE_APP. Returns false (leaving @p out untouched) when any is
     * missing — i.e. the gate is not running against a sim we can drive.
     */
    static bool fromEnvironment(SimControl &out);

    /**
     * @brief POST /SetState/<app>/<state> (empty body), returning the sim verdict.
     *
     * @param state one of the sim's states: "Active", "Inactive", "NotRunning".
     * @retval true when the sim replies that the SetState command succeeded.
     */
    bool setState(const std::string &state) const;

    bool setActive() const { return setState("Active"); }
    bool setInactive() const { return setState("Inactive"); }

    /**
     * @brief GET /GetState/<app>, returning the reported state token.
     *
     * @return the state the sim reports (e.g. "Active", "Inactive"), or an empty
     *         string if the query could not be answered.
     */
    std::string getState() const;

    const std::string &app() const { return m_app; }

private:
    std::string m_host;
    std::string m_port;
    std::string m_app;
};
} // namespace rialto::conformance

#endif // RIALTO_CONFORMANCE_SIM_CONTROL_H_
