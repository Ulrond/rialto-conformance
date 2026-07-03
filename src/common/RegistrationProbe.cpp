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

#include "conformance/RegistrationProbe.h"

#include <gst/gst.h>

#include <cstdlib>

namespace rialto::conformance
{
int runRegistrationProbeIfRequested()
{
    const char *factoryName = std::getenv("RIALTO_CONFORMANCE_PROBE_FACTORY");
    if (factoryName == nullptr)
        return -1; // not a probe invocation: run the suite normally

    // Fresh registry scan honours this process's rank env (RIALTO_SOCKET_PATH /
    // RIALTO_SINKS_RANK): the parent case points GST_REGISTRY at a private file so
    // the rialtosinks plugin_init runs here rather than loading a cached result.
    gst_init(nullptr, nullptr);

    GstElementFactory *factory = gst_element_factory_find(factoryName);
    const bool registered = (factory != nullptr);
    if (factory != nullptr)
        gst_object_unref(factory);

    return registered ? 0 : 1;
}

} // namespace rialto::conformance
