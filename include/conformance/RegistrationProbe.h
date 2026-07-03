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

#ifndef RIALTO_CONFORMANCE_REGISTRATIONPROBE_H_
#define RIALTO_CONFORMANCE_REGISTRATIONPROBE_H_

/**
 * @file RegistrationProbe.h
 *
 * A one-shot "is this GStreamer element factory registered?" probe used to test
 * the rank-gated registration contract (RC-CORE-MSE-002).
 *
 * The rialtosinks plugin computes its rank from RIALTO_SOCKET_PATH /
 * RIALTO_SINKS_RANK at plugin-init time and, when the rank is 0, registers no
 * elements at all. That decision is taken once per process during the registry
 * scan, so it cannot be re-observed in the gate process (which already has the
 * sinks loaded with a non-zero rank). The probe therefore runs in a fresh
 * child process — a re-exec of this same binary with RIALTO_CONFORMANCE_PROBE_FACTORY
 * set and a private GST_REGISTRY — which does a minimal gst_init, looks the
 * named factory up, and exits with the result. The parent case (in
 * SinkRegistrationTests.cpp) drives that child with the rank env cleared
 * (expect absent) and forced to 1 (expect present).
 */

namespace rialto::conformance
{
/**
 * Probe entry point, called from main() before the test harness starts.
 *
 * When RIALTO_CONFORMANCE_PROBE_FACTORY is set, this runs the probe and returns
 * a process exit code to hand straight back from main():
 *   - 0  the named factory is registered in this (freshly scanned) process,
 *   - 1  it is not registered.
 * When the variable is unset (the normal path) it returns -1 and the caller
 * proceeds to run the test suite as usual.
 */
int runRegistrationProbeIfRequested();

} // namespace rialto::conformance

#endif // RIALTO_CONFORMANCE_REGISTRATIONPROBE_H_
