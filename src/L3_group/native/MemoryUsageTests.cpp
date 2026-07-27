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
 * @file MemoryUsageTests.cpp
 *
 * L3 — a NON-FUNCTIONAL memory-usage dimension for Firebolt interface.
 *
 * Unlike the interface-conformance cases (which assert the published contract),
 * this case exercises the client API under repeated object-lifecycle churn and
 * observes process memory, to (a) REPORT the backend's memory envelope and
 * (b) GUARD against a leak — RSS growing without bound across steady-state
 * create/destroy cycles.
 *
 * Two things are measured, both from Linux /proc:
 *   - the test (client) process itself — always available (/proc/self/status);
 *   - the RialtoServer process — best-effort, located by matching /proc/<pid>/comm
 *     (the software platform brings one up via the ServerManagerSim). If it can
 *     not be found the server figure is reported as unavailable and the client
 *     leak guard still runs.
 *
 * The guard is deliberately RELATIVE (a percentage of the post-warmup baseline
 * plus a small absolute floor), never an absolute RSS ceiling: an absolute
 * budget would be SoC-dependent and would violate the platform-agnostic
 * invariant. A leak shows as unbounded *growth* on any platform; that is what is
 * asserted. Raw peak/steady numbers are logged for a human to track across
 * releases (rialto's memory-optimisation work is exactly the kind of change this
 * makes observable).
 *
 * The churn workload is object-lifecycle only (create/destroy
 * IMediaPipelineCapabilities + query), so it needs no playback data path — it
 * runs on the software platform now.
 *
 * Tier: EXTENDED. This is a non-functional property, not a catalogue-derived
 * interface requirement, so it is kept out of the CORE (transform-safety) gate;
 * it runs under RIALTO_CONFORMANCE_TIER=all|extended.
 */

#include <ut.h>
#include <ut_log.h>

#include "conformance/Surfaces.h"
#include "conformance/TierGate.h"

#include "IMediaPipelineCapabilities.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <string>
#include <unistd.h>

using namespace firebolt::rialto;
using rialto::conformance::NativeClientSurface;

namespace
{
/// Number of create/destroy cycles run before the reference sample, so lazy
/// first-touch allocations (client-lib init, server-side caches) settle — a leak
/// guard must measure steady state, not one-off warmup cost.
constexpr int kWarmupCycles = 25;
/// The measured churn: growth across these cycles is what the guard bounds.
constexpr int kMeasureCycles = 250;

/// Relative leak tolerance: post-warmup RSS growth may be up to this percentage
/// of the baseline, or the absolute floor below, whichever is larger. The floor
/// absorbs allocator page-retention on small baselines; the percentage keeps the
/// bound platform-relative rather than an absolute (SoC-specific) ceiling.
constexpr long kTolerancePercent = 10;
constexpr long kMinToleranceKb = 4096; // 4 MiB floor

/// The RialtoServer process name as it appears in /proc/<pid>/comm (comm is
/// truncated to 15 chars by the kernel; this name is well within that).
constexpr const char *kServerComm = "RialtoServer";

/// Read a "VmRSS:"/"VmHWM:"-style size (in kB) from /proc/<pid>/status.
/// Returns -1 if the file or field cannot be read.
long readStatusKb(long pid, const char *field)
{
    char path[64];
    std::snprintf(path, sizeof(path), "/proc/%ld/status", pid);
    std::ifstream f(path);
    if (!f)
    {
        return -1;
    }
    const std::size_t flen = std::strlen(field);
    std::string line;
    while (std::getline(f, line))
    {
        if (line.compare(0, flen, field) == 0)
        {
            long kb = -1;
            // e.g. "VmRSS:\t   12345 kB"
            std::sscanf(line.c_str() + flen, " %ld", &kb);
            return kb;
        }
    }
    return -1;
}

long readSelfRssKb()
{
    return readStatusKb(static_cast<long>(::getpid()), "VmRSS:");
}

/// Best-effort scan of /proc for the RialtoServer process. Returns its pid, or
/// -1 if no matching process is found (the server figure is then reported as
/// unavailable — the client-side guard is unaffected).
long findServerPid()
{
    DIR *proc = ::opendir("/proc");
    if (proc == nullptr)
    {
        return -1;
    }
    long found = -1;
    for (struct dirent *ent = ::readdir(proc); ent != nullptr; ent = ::readdir(proc))
    {
        char *end = nullptr;
        const long pid = std::strtol(ent->d_name, &end, 10);
        if (end == ent->d_name || *end != '\0' || pid <= 0)
        {
            continue; // not a /proc/<pid> entry
        }
        char path[64];
        std::snprintf(path, sizeof(path), "/proc/%ld/comm", pid);
        std::ifstream f(path);
        if (!f)
        {
            continue;
        }
        std::string comm;
        std::getline(f, comm);
        if (comm == kServerComm)
        {
            found = pid;
            break;
        }
    }
    ::closedir(proc);
    return found;
}

class L3MemoryUsageTests : public NativeClientSurface
{
};
} // namespace

UT_ADD_TEST_TO_GROUP(L3MemoryUsageTests, UT_TESTS_L3);

/**
 * Repeated create/destroy of IMediaPipelineCapabilities (with a query each cycle)
 * must reach a steady memory state: after a warmup, further churn must not grow
 * the client process RSS beyond a small relative tolerance. Reports the client
 * and (best-effort) server RSS/peak so the memory envelope is visible per run.
 */
UT_ADD_TEST(L3MemoryUsageTests, CapabilitiesLifecycleChurnDoesNotLeak)
{
    CONFORMANCE_EXTENDED_TEST();

    auto factory = IMediaPipelineCapabilitiesFactory::createFactory();
    UT_ASSERT_NOT_NULL_FATAL(factory.get());

    // The churn unit: construct a capabilities object, exercise a query so the
    // full request/response path allocates, then release it.
    auto churn = [&factory](int cycles)
    {
        for (int i = 0; i < cycles; ++i)
        {
            auto caps = factory->createMediaPipelineCapabilities();
            if (caps)
            {
                (void)caps->getSupportedMimeTypes(MediaSourceType::VIDEO);
            }
        }
    };

    const long serverPid = findServerPid();

    // Settle first-touch allocations before the reference sample.
    churn(kWarmupCycles);

    const long selfBaseKb = readSelfRssKb();
    if (selfBaseKb <= 0)
    {
        // No readable /proc (non-Linux host): cannot measure — skip rather than
        // assert on a figure we do not have.
        UT_LOG("[memory-usage] /proc unavailable; cannot sample RSS — skipping");
        UT_IGNORE_TEST();
        return;
    }
    const long serverBaseKb = (serverPid > 0) ? readStatusKb(serverPid, "VmRSS:") : -1;
    UT_LOG("[memory-usage] baseline (after %d warmup cycles): client_rss=%ld kB, server_rss=%ld kB (pid %ld)",
           kWarmupCycles, selfBaseKb, serverBaseKb, serverPid);

    // The measured churn.
    churn(kMeasureCycles);

    const long selfEndKb = readSelfRssKb();
    const long selfHwmKb = readStatusKb(static_cast<long>(::getpid()), "VmHWM:");
    const long serverEndKb = (serverPid > 0) ? readStatusKb(serverPid, "VmRSS:") : -1;
    const long selfGrowthKb = selfEndKb - selfBaseKb;

    UT_LOG("[memory-usage] after %d churn cycles: client_rss=%ld kB (peak %ld kB, growth %ld kB), server_rss=%ld kB",
           kMeasureCycles, selfEndKb, selfHwmKb, selfGrowthKb, serverEndKb);
    if (serverBaseKb > 0 && serverEndKb > 0)
    {
        UT_LOG("[memory-usage] server growth over churn: %ld kB", serverEndKb - serverBaseKb);
    }

    // Relative leak guard on the client process: steady-state growth must stay
    // within tolerance. Exact-zero growth is unrealistic (the allocator retains
    // freed pages), so the bound is a percentage of the baseline plus a floor.
    const long toleranceKb = std::max(kMinToleranceKb, (selfBaseKb * kTolerancePercent) / 100);
    UT_LOG("[memory-usage] leak guard: client growth %ld kB must be <= tolerance %ld kB (%ld%% of %ld kB, floor %ld kB)",
           selfGrowthKb, toleranceKb, kTolerancePercent, selfBaseKb, kMinToleranceKb);
    UT_ASSERT_TRUE(selfGrowthKb <= toleranceKb);
}
