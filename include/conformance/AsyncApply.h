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

#ifndef RIALTO_CONFORMANCE_ASYNC_APPLY_H_
#define RIALTO_CONFORMANCE_ASYNC_APPLY_H_

/**
 * @file AsyncApply.h
 *
 * Polled read-back for asynchronously applied properties.
 *
 * Property setters across the Rialto client surface (volume, mute, sync,
 * use-buffering, buffering limit, stream sync mode, ...) enqueue their write to
 * the server's worker thread and return before it has been applied. A read-back
 * issued immediately after the write is therefore racing the worker: it passes
 * only when the worker happens to win, which makes any such case intermittently
 * flaky — and more so on a loaded host or a slower target.
 *
 * Every case that reads back an async property must poll. This is the shared
 * helper for that; it lives in a header rather than one test file so a new case
 * cannot accidentally reintroduce the race by not knowing the local copy existed.
 */

#include <chrono>
#include <thread>

namespace rialto::conformance
{
/// How long a written property is given to become observable.
constexpr std::chrono::milliseconds kApplyDeadline{5000};
/// Gap between read-back attempts while waiting.
constexpr std::chrono::milliseconds kApplyPoll{50};

/**
 * @brief Poll @p read until it reports the written value has applied.
 *
 * @param read Predicate performing the read-back; returns true once the value
 *             observed matches what was written.
 * @retval true  the read-back reflected the written value within kApplyDeadline.
 * @retval false it never did — a genuine conformance failure, not a race.
 */
template <typename ReadFn>
bool pollUntilApplied(ReadFn read)
{
    const auto deadline = std::chrono::steady_clock::now() + kApplyDeadline;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (read())
        {
            return true;
        }
        std::this_thread::sleep_for(kApplyPoll);
    }
    return read();
}

} // namespace rialto::conformance

#endif // RIALTO_CONFORMANCE_ASYNC_APPLY_H_
