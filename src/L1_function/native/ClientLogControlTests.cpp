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
 * @file ClientLogControlTests.cpp
 *
 * L1 — function testing for Surface B: IClientLogControl / IClientLogHandler.
 *
 * IClientLogControl lets an application redirect the Rialto client library's log
 * output to its own handler. These cases exercise the published contract: the
 * singleton factory, handler delivery (with record fields + ignoreLogLevels),
 * handler replacement, and cancellation — never Rialto internals.
 *
 * Coverage trace: coverage/matrix.yaml / rc-core-catalog.yaml rows
 * RC-CORE-LOG-001 (factory + singleton), RC-CORE-LOG-002 (handler receives
 * populated records), RC-CORE-LOG-003 (replace), RC-CORE-LOG-004 (cancel).
 */

#include <ut.h>

#include "conformance/Surfaces.h"
#include "conformance/TierGate.h"

#include "IClientLogControl.h"
#include "IClientLogHandler.h"
#include "IMediaPipelineCapabilities.h"

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

using namespace firebolt::rialto;
using rialto::conformance::NativeClientSurface;

namespace
{
constexpr auto kLogTimeout = std::chrono::seconds(5);

/// True iff @p level is one of the defined IClientLogHandler::Level enumerators.
bool isDefinedLevel(IClientLogHandler::Level level)
{
    switch (level)
    {
    case IClientLogHandler::Level::Fatal:
    case IClientLogHandler::Level::Error:
    case IClientLogHandler::Level::Warning:
    case IClientLogHandler::Level::Milestone:
    case IClientLogHandler::Level::Info:
    case IClientLogHandler::Level::Debug:
    case IClientLogHandler::Level::External:
        return true;
    }
    return false;
}

/**
 * Records the log callbacks it receives. log() is invoked from Rialto client
 * threads, so access is mutex-guarded; a condition variable lets a case wait for
 * records to arrive. The handler itself never logs (no re-entrancy).
 */
class RecordingLogHandler : public IClientLogHandler
{
public:
    struct Record
    {
        Level level;
        std::string file;
        int line;
        std::string function;
        std::string message;
    };

    void log(Level level, const std::string &file, int line, const std::string &function,
             const std::string &message) override
    {
        {
            std::lock_guard<std::mutex> lock{m_mutex};
            m_records.push_back({level, file, line, function, message});
        }
        m_cv.notify_all();
    }

    bool waitForAtLeast(std::size_t n, std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock{m_mutex};
        return m_cv.wait_for(lock, timeout, [&] { return m_records.size() >= n; });
    }

    std::size_t count()
    {
        std::lock_guard<std::mutex> lock{m_mutex};
        return m_records.size();
    }

    Record first()
    {
        std::lock_guard<std::mutex> lock{m_mutex};
        return m_records.front();
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::vector<Record> m_records;
};

/// Obtain the IClientLogControl singleton (nullptr on factory failure).
IClientLogControl *clientLogControl()
{
    auto factory = IClientLogControlFactory::createFactory();
    return factory ? &factory->createClientLogControl() : nullptr;
}

/// Exercise the client library so it emits log records to any registered handler.
void triggerClientLogActivity()
{
    auto factory = IMediaPipelineCapabilitiesFactory::createFactory();
    if (factory)
    {
        auto caps = factory->createMediaPipelineCapabilities();
        if (caps)
        {
            (void)caps->getSupportedMimeTypes(MediaSourceType::VIDEO);
        }
    }
}

class L1ClientLogControlTests : public NativeClientSurface
{
protected:
    /// Always leave the singleton with no handler so a registered handler never
    /// leaks into later suites.
    void TearDown() override
    {
        if (auto *log = clientLogControl())
        {
            log->registerLogHandler(nullptr, false);
        }
        NativeClientSurface::TearDown();
    }
};
} // namespace

UT_ADD_TEST_TO_GROUP(L1ClientLogControlTests, UT_TESTS_L1);

/**
 * RC-CORE-LOG-001 — the factory is non-null and createClientLogControl() returns
 * the singleton IClientLogControl (the same instance across calls).
 */
UT_ADD_TEST(L1ClientLogControlTests, FactoryReturnsSingleton)
{
    CONFORMANCE_CORE_TEST();

    auto factory = IClientLogControlFactory::createFactory();
    UT_ASSERT_NOT_NULL_FATAL(factory.get());

    IClientLogControl &a = factory->createClientLogControl();
    IClientLogControl &b = factory->createClientLogControl();
    UT_ASSERT_EQUAL(&a, &b);
}

/**
 * RC-CORE-LOG-002 — registerLogHandler(handler, ignoreLogLevels=true) returns
 * true and the handler receives client log records with a defined Level and
 * populated file/function/message fields.
 */
UT_ADD_TEST(L1ClientLogControlTests, HandlerReceivesPopulatedRecords)
{
    CONFORMANCE_CORE_TEST();

    IClientLogControl *log = clientLogControl();
    UT_ASSERT_NOT_NULL_FATAL(log);

    auto handler = std::make_shared<RecordingLogHandler>();
    UT_ASSERT_TRUE(log->registerLogHandler(handler, true));

    triggerClientLogActivity();
    UT_ASSERT_TRUE(handler->waitForAtLeast(1, kLogTimeout));

    const RecordingLogHandler::Record record = handler->first();
    UT_ASSERT_TRUE(isDefinedLevel(record.level));
    UT_ASSERT_FALSE(record.file.empty());
    UT_ASSERT_FALSE(record.function.empty());
    UT_ASSERT_FALSE(record.message.empty());
}

/**
 * RC-CORE-LOG-003 — registering a new handler replaces the previous one: after
 * the swap the new handler receives records.
 */
UT_ADD_TEST(L1ClientLogControlTests, RegisteringNewHandlerReplacesPrevious)
{
    CONFORMANCE_CORE_TEST();

    IClientLogControl *log = clientLogControl();
    UT_ASSERT_NOT_NULL_FATAL(log);

    auto first = std::make_shared<RecordingLogHandler>();
    UT_ASSERT_TRUE(log->registerLogHandler(first, true));
    triggerClientLogActivity();

    auto second = std::make_shared<RecordingLogHandler>();
    UT_ASSERT_TRUE(log->registerLogHandler(second, true));

    triggerClientLogActivity();
    UT_ASSERT_TRUE(second->waitForAtLeast(1, kLogTimeout));
}

/**
 * RC-CORE-LOG-004 — registerLogHandler(nullptr, ...) cancels delivery: no
 * further log() callbacks are made after the handler is cleared.
 */
UT_ADD_TEST(L1ClientLogControlTests, NullHandlerCancelsDelivery)
{
    CONFORMANCE_CORE_TEST();

    IClientLogControl *log = clientLogControl();
    UT_ASSERT_NOT_NULL_FATAL(log);

    auto handler = std::make_shared<RecordingLogHandler>();
    UT_ASSERT_TRUE(log->registerLogHandler(handler, true));
    triggerClientLogActivity();
    UT_ASSERT_TRUE(handler->waitForAtLeast(1, kLogTimeout));

    // Cancel, let any in-flight records settle, then snapshot.
    UT_ASSERT_TRUE(log->registerLogHandler(nullptr, false));
    handler->waitForAtLeast(SIZE_MAX, std::chrono::milliseconds(200)); // brief settle
    const std::size_t afterCancel = handler->count();

    // Further activity must not reach the cleared handler.
    triggerClientLogActivity();
    handler->waitForAtLeast(afterCancel + 1, std::chrono::milliseconds(500));
    UT_ASSERT_EQUAL(handler->count(), afterCancel);
}
