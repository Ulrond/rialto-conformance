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
 * @file KeysSessionTests.cpp
 *
 * L1 — function testing for Surface B: the IMediaKeys key-session lifecycle,
 * driven against the live RialtoServer with the suite's ClearKey OpenCDM backend
 * (backends/opencdm/clearkey — a real W3C ClearKey exchange: keyids init data →
 * license-request JSON via onLicenseRequest → JWK response via updateSession →
 * key statuses). Cases exercising the positive ClearKey flow gate on the
 * `drm.clearkey` platform feature; the negative session-id contract is
 * key-system-independent and runs wherever any key system is available.
 *
 * Coverage trace: coverage/rc-core-catalog.yaml / matrix.yaml rows
 * RC-CORE-KEYS-001 (create session), -002 (generateRequest → onLicenseRequest),
 * -003 (updateSession → onKeyStatusesChanged), -005 (close/remove/release OK),
 * -006 (BAD_SESSION_ID), -009 (containsKey), -011 (store/maintenance queries),
 * -012 (weak_ptr client).
 */

#include <ut.h>
#include <ut_log.h>

#include "conformance/CapabilityGate.h"
#include "conformance/Surfaces.h"
#include "conformance/TierGate.h"

#include "IMediaKeys.h"
#include "IMediaKeysClient.h"
#include "MediaCommon.h"

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
constexpr const char *kClearKey = "org.w3.clearkey";

// A fixed 16-byte test key id + key, and their base64url forms (no padding).
// kid = 0x30..0x3F ("0123456789:;<=>?"), key = 16 x 0x41 ("A").
const std::vector<uint8_t> kKeyId{0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
                                  0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F};
constexpr const char *kKeyIdB64 = "MDEyMzQ1Njc4OTo7PD0-Pw";
constexpr const char *kKeyB64 = "QUFBQUFBQUFBQUFBQUFBQQ";

// W3C ClearKey init data (keyids) + license response (JWK set) for the key above.
const std::string kInitDataJson = std::string{"{\"kids\":[\""} + kKeyIdB64 + "\"]}";
const std::string kJwkResponse =
    std::string{"{\"keys\":[{\"kty\":\"oct\",\"kid\":\""} + kKeyIdB64 + "\",\"k\":\"" + kKeyB64 + "\"}],\"type\":\"temporary\"}";

std::vector<uint8_t> toBytes(const std::string &text)
{
    return std::vector<uint8_t>{text.begin(), text.end()};
}

constexpr std::chrono::milliseconds kCallbackTimeout{5000};

/**
 * An IMediaKeysClient recording the license-request and key-statuses callbacks
 * so a case can wait for and inspect them. Thread-safe (callbacks arrive on the
 * client IPC thread).
 */
class RecordingMediaKeysClient : public IMediaKeysClient
{
public:
    void onLicenseRequest(int32_t keySessionId, const std::vector<unsigned char> &licenseRequestMessage,
                          const std::string &url) override
    {
        {
            std::lock_guard<std::mutex> lock{m_mutex};
            m_licenseRequestSessionId = keySessionId;
            m_licenseRequest.assign(licenseRequestMessage.begin(), licenseRequestMessage.end());
            m_licenseRequestUrl = url;
            m_licenseRequestReceived = true;
        }
        m_cv.notify_all();
    }

    void onLicenseRenewal(int32_t, const std::vector<unsigned char> &) override {}

    void onKeyStatusesChanged(int32_t keySessionId, const KeyStatusVector &keyStatuses) override
    {
        {
            std::lock_guard<std::mutex> lock{m_mutex};
            m_keyStatusesSessionId = keySessionId;
            m_keyStatuses = keyStatuses;
            m_keyStatusesReceived = true;
        }
        m_cv.notify_all();
    }

    bool waitForLicenseRequest(std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock{m_mutex};
        return m_cv.wait_for(lock, timeout, [&] { return m_licenseRequestReceived; });
    }

    bool waitForKeyStatuses(std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock{m_mutex};
        return m_cv.wait_for(lock, timeout, [&] { return m_keyStatusesReceived; });
    }

    std::string licenseRequestText()
    {
        std::lock_guard<std::mutex> lock{m_mutex};
        return m_licenseRequest;
    }

    int32_t licenseRequestSessionId()
    {
        std::lock_guard<std::mutex> lock{m_mutex};
        return m_licenseRequestSessionId;
    }

    KeyStatusVector keyStatuses()
    {
        std::lock_guard<std::mutex> lock{m_mutex};
        return m_keyStatuses;
    }

    int32_t keyStatusesSessionId()
    {
        std::lock_guard<std::mutex> lock{m_mutex};
        return m_keyStatusesSessionId;
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_licenseRequestReceived = false;
    bool m_keyStatusesReceived = false;
    int32_t m_licenseRequestSessionId = -1;
    int32_t m_keyStatusesSessionId = -1;
    std::string m_licenseRequest;
    std::string m_licenseRequestUrl;
    KeyStatusVector m_keyStatuses;
};

/// Create IMediaKeys for ClearKey (nullptr if the factory refuses).
std::unique_ptr<IMediaKeys> createClearKeyMediaKeys()
{
    auto factory = IMediaKeysFactory::createFactory();
    if (!factory)
        return nullptr;
    return factory->createMediaKeys(kClearKey);
}

class L1KeysSessionTests : public NativeClientSurface
{
};
} // namespace

UT_ADD_TEST_TO_GROUP(L1KeysSessionTests, UT_TESTS_L1);

/**
 * RC-CORE-KEYS-001 — createMediaKeys(supported system) yields an instance, and
 * createKeySession writes a valid (non-negative) session id and registers the
 * client.
 */
UT_ADD_TEST(L1KeysSessionTests, CreateKeySessionWritesValidId)
{
    CONFORMANCE_CORE_TEST();
    CONFORMANCE_REQUIRE_CAP("drm.clearkey");

    std::unique_ptr<IMediaKeys> mediaKeys = createClearKeyMediaKeys();
    UT_ASSERT_NOT_NULL_FATAL(mediaKeys.get());

    auto client = std::make_shared<RecordingMediaKeysClient>();
    int32_t sessionId = -1;
    UT_ASSERT_EQUAL(mediaKeys->createKeySession(KeySessionType::TEMPORARY, client, sessionId),
                    MediaKeyErrorStatus::OK);
    UT_LOG("[keys] created key session id=%d", sessionId);
    UT_ASSERT_TRUE(sessionId >= 0);

    mediaKeys->closeKeySession(sessionId);
}

/**
 * RC-CORE-KEYS-002 — generateRequest triggers onLicenseRequest asynchronously
 * with the request message (the W3C ClearKey license-request JSON naming the
 * requested kid) and a url (empty when none applies).
 */
UT_ADD_TEST(L1KeysSessionTests, GenerateRequestDeliversLicenseRequest)
{
    CONFORMANCE_CORE_TEST();
    CONFORMANCE_REQUIRE_CAP("drm.clearkey");

    std::unique_ptr<IMediaKeys> mediaKeys = createClearKeyMediaKeys();
    UT_ASSERT_NOT_NULL_FATAL(mediaKeys.get());

    auto client = std::make_shared<RecordingMediaKeysClient>();
    int32_t sessionId = -1;
    UT_ASSERT_EQUAL(mediaKeys->createKeySession(KeySessionType::TEMPORARY, client, sessionId),
                    MediaKeyErrorStatus::OK);

    UT_ASSERT_EQUAL(mediaKeys->generateRequest(sessionId, InitDataType::KEY_IDS, toBytes(kInitDataJson)),
                    MediaKeyErrorStatus::OK);

    UT_ASSERT_TRUE_FATAL(client->waitForLicenseRequest(kCallbackTimeout));
    UT_ASSERT_EQUAL(client->licenseRequestSessionId(), sessionId);
    const std::string request = client->licenseRequestText();
    UT_LOG("[keys] license request: %s", request.c_str());
    // The ClearKey request must name the requested kid.
    UT_ASSERT_TRUE(request.find(kKeyIdB64) != std::string::npos);

    mediaKeys->closeKeySession(sessionId);
}

/**
 * RC-CORE-KEYS-003 + RC-CORE-KEYS-009 — updateSession applies a licence response
 * (the ClearKey JWK set) and surfaces the key state via onKeyStatusesChanged
 * (USABLE for the supplied kid); containsKey then reports the session holds the
 * key (and not an arbitrary other key).
 */
UT_ADD_TEST(L1KeysSessionTests, UpdateSessionSurfacesUsableKeyStatus)
{
    CONFORMANCE_CORE_TEST();
    CONFORMANCE_REQUIRE_CAP("drm.clearkey");

    std::unique_ptr<IMediaKeys> mediaKeys = createClearKeyMediaKeys();
    UT_ASSERT_NOT_NULL_FATAL(mediaKeys.get());

    auto client = std::make_shared<RecordingMediaKeysClient>();
    int32_t sessionId = -1;
    UT_ASSERT_EQUAL(mediaKeys->createKeySession(KeySessionType::TEMPORARY, client, sessionId),
                    MediaKeyErrorStatus::OK);
    UT_ASSERT_EQUAL(mediaKeys->generateRequest(sessionId, InitDataType::KEY_IDS, toBytes(kInitDataJson)),
                    MediaKeyErrorStatus::OK);
    UT_ASSERT_TRUE_FATAL(client->waitForLicenseRequest(kCallbackTimeout));

    // Apply the JWK license response.
    UT_ASSERT_EQUAL(mediaKeys->updateSession(sessionId, toBytes(kJwkResponse)), MediaKeyErrorStatus::OK);

    // KEYS-003: the key statuses arrive and report the supplied kid USABLE.
    UT_ASSERT_TRUE_FATAL(client->waitForKeyStatuses(kCallbackTimeout));
    UT_ASSERT_EQUAL(client->keyStatusesSessionId(), sessionId);
    const KeyStatusVector statuses = client->keyStatuses();
    UT_ASSERT_TRUE_FATAL(!statuses.empty());
    bool sawUsableKid = false;
    for (const auto &entry : statuses)
    {
        if (entry.first == kKeyId && entry.second == KeyStatus::USABLE)
        {
            sawUsableKid = true;
        }
    }
    UT_LOG("[keys] key statuses: %zu entries, usable-kid=%d", statuses.size(), sawUsableKid);
    UT_ASSERT_TRUE(sawUsableKid);

    // KEYS-009: containsKey answers per key id.
    UT_ASSERT_TRUE(mediaKeys->containsKey(sessionId, kKeyId));
    const std::vector<uint8_t> otherKid(16, 0x99);
    UT_ASSERT_FALSE(mediaKeys->containsKey(sessionId, otherKid));

    mediaKeys->closeKeySession(sessionId);
}

/**
 * RC-CORE-KEYS-005 — closeKeySession / removeKeySession / releaseKeySession
 * return OK for a valid open session.
 */
UT_ADD_TEST(L1KeysSessionTests, SessionTeardownReturnsOk)
{
    CONFORMANCE_CORE_TEST();
    CONFORMANCE_REQUIRE_CAP("drm.clearkey");

    std::unique_ptr<IMediaKeys> mediaKeys = createClearKeyMediaKeys();
    UT_ASSERT_NOT_NULL_FATAL(mediaKeys.get());

    auto client = std::make_shared<RecordingMediaKeysClient>();
    int32_t sessionId = -1;
    UT_ASSERT_EQUAL(mediaKeys->createKeySession(KeySessionType::TEMPORARY, client, sessionId),
                    MediaKeyErrorStatus::OK);
    UT_ASSERT_EQUAL(mediaKeys->generateRequest(sessionId, InitDataType::KEY_IDS, toBytes(kInitDataJson)),
                    MediaKeyErrorStatus::OK);
    UT_ASSERT_TRUE_FATAL(client->waitForLicenseRequest(kCallbackTimeout));
    UT_ASSERT_EQUAL(mediaKeys->updateSession(sessionId, toBytes(kJwkResponse)), MediaKeyErrorStatus::OK);

    UT_ASSERT_EQUAL(mediaKeys->removeKeySession(sessionId), MediaKeyErrorStatus::OK);
    UT_ASSERT_EQUAL(mediaKeys->closeKeySession(sessionId), MediaKeyErrorStatus::OK);
    UT_ASSERT_EQUAL(mediaKeys->releaseKeySession(sessionId), MediaKeyErrorStatus::OK);
}

/**
 * RC-CORE-KEYS-006 — session operations on an unknown session id fail with the
 * documented error. generateRequest / updateSession / removeKeySession return
 * BAD_SESSION_ID. closeKeySession returns FAIL: the reference implementation's
 * service layer rejects an unknown id from its own session registry before the
 * BAD_SESSION_ID-returning layer is reached, while the header documents
 * BAD_SESSION_ID for close too — a header/implementation discrepancy recorded as
 * interface-definition-gaps.md IDG-006. The suite asserts the observed reference
 * contract (transform-safety): an error, specifically FAIL, never OK.
 */
UT_ADD_TEST(L1KeysSessionTests, UnknownSessionIdReturnsBadSessionId)
{
    CONFORMANCE_CORE_TEST();
    CONFORMANCE_REQUIRE_CAP("drm.clearkey");

    std::unique_ptr<IMediaKeys> mediaKeys = createClearKeyMediaKeys();
    UT_ASSERT_NOT_NULL_FATAL(mediaKeys.get());

    constexpr int32_t kUnknownSession = 987654;
    UT_ASSERT_EQUAL(mediaKeys->generateRequest(kUnknownSession, InitDataType::KEY_IDS, toBytes(kInitDataJson)),
                    MediaKeyErrorStatus::BAD_SESSION_ID);
    UT_ASSERT_EQUAL(mediaKeys->updateSession(kUnknownSession, toBytes(kJwkResponse)),
                    MediaKeyErrorStatus::BAD_SESSION_ID);
    // close: FAIL from the service session-registry miss (IDG-006), not
    // BAD_SESSION_ID as the header documents.
    UT_ASSERT_EQUAL(mediaKeys->closeKeySession(kUnknownSession), MediaKeyErrorStatus::FAIL);
    UT_ASSERT_EQUAL(mediaKeys->removeKeySession(kUnknownSession), MediaKeyErrorStatus::BAD_SESSION_ID);
    UT_ASSERT_FALSE(mediaKeys->containsKey(kUnknownSession, kKeyId));
}

/**
 * RC-CORE-KEYS-011 — the store/maintenance queries return a status and write
 * their out-params rather than crashing or hanging: getDrmTime and
 * getCdmKeySessionId succeed on the ClearKey backend (the CDM session id is the
 * backend's session name); the store hashes / LDL limit / metrics queries and the
 * deleteDrmStore / deleteKeyStore mutations return a defined status for a
 * store-less CDM (not asserted OK — ClearKey keeps no persistent store).
 */
UT_ADD_TEST(L1KeysSessionTests, StoreAndMaintenanceQueriesAnswer)
{
    CONFORMANCE_CORE_TEST();
    CONFORMANCE_REQUIRE_CAP("drm.clearkey");

    std::unique_ptr<IMediaKeys> mediaKeys = createClearKeyMediaKeys();
    UT_ASSERT_NOT_NULL_FATAL(mediaKeys.get());

    auto client = std::make_shared<RecordingMediaKeysClient>();
    int32_t sessionId = -1;
    UT_ASSERT_EQUAL(mediaKeys->createKeySession(KeySessionType::TEMPORARY, client, sessionId),
                    MediaKeyErrorStatus::OK);
    UT_ASSERT_EQUAL(mediaKeys->generateRequest(sessionId, InitDataType::KEY_IDS, toBytes(kInitDataJson)),
                    MediaKeyErrorStatus::OK);
    UT_ASSERT_TRUE_FATAL(client->waitForLicenseRequest(kCallbackTimeout));

    // getDrmTime: succeeds (the ClearKey backend reports its epoch clock).
    uint64_t drmTime = 1;
    UT_ASSERT_EQUAL(mediaKeys->getDrmTime(drmTime), MediaKeyErrorStatus::OK);

    // getCdmKeySessionId: succeeds for a constructed session and is non-empty.
    std::string cdmSessionId;
    UT_ASSERT_EQUAL(mediaKeys->getCdmKeySessionId(sessionId, cdmSessionId), MediaKeyErrorStatus::OK);
    UT_LOG("[keys] cdm session id: %s", cdmSessionId.c_str());
    UT_ASSERT_TRUE(!cdmSessionId.empty());

    // Store-less CDM: each query answers with a defined status (logged, not
    // asserted OK) and must not hang or crash.
    std::vector<unsigned char> hash;
    const MediaKeyErrorStatus drmStoreHash = mediaKeys->getDrmStoreHash(hash);
    const MediaKeyErrorStatus keyStoreHash = mediaKeys->getKeyStoreHash(hash);
    uint32_t ldlLimit = 0;
    const MediaKeyErrorStatus ldl = mediaKeys->getLdlSessionsLimit(ldlLimit);
    uint32_t lastError = 0;
    const MediaKeyErrorStatus lastDrmError = mediaKeys->getLastDrmError(sessionId, lastError);
    std::vector<uint8_t> metrics;
    const MediaKeyErrorStatus metricStatus = mediaKeys->getMetricSystemData(metrics);
    UT_LOG("[keys] store queries: drmStoreHash=%d keyStoreHash=%d ldl=%d lastDrmError=%d metrics=%d",
           static_cast<int>(drmStoreHash), static_cast<int>(keyStoreHash), static_cast<int>(ldl),
           static_cast<int>(lastDrmError), static_cast<int>(metricStatus));

    // Store deletion (also KEYS-011): deleteDrmStore / deleteKeyStore answer a
    // defined status on the store-less ClearKey CDM (no persistent store to
    // clear) without hanging — the store-mutation half of the maintenance surface.
    const MediaKeyErrorStatus drmStoreDeleted = mediaKeys->deleteDrmStore();
    const MediaKeyErrorStatus keyStoreDeleted = mediaKeys->deleteKeyStore();
    UT_LOG("[keys] store deletion: deleteDrmStore=%d deleteKeyStore=%d",
           static_cast<int>(drmStoreDeleted), static_cast<int>(keyStoreDeleted));

    mediaKeys->closeKeySession(sessionId);
}

/**
 * RC-CORE-KEYS-013 — selectKeyId(sessionId, keyId) selects which key in a
 * multi-key session decrypts subsequent buffers. On a valid, key-loaded session
 * (the full generateRequest -> onLicenseRequest -> updateSession ->
 * onKeyStatusesChanged exchange) the call is answered with a defined status and
 * the session is recognised (not BAD_SESSION_ID). ClearKey sessions carry a
 * single key, so the ClearKey backend answers a defined status rather than
 * performing a selection — the call must still be handled without hanging. This
 * firms into an OK-selection assertion on a multi-key vendor CDM.
 */
UT_ADD_TEST(L1KeysSessionTests, SelectKeyIdAnswersForValidSession)
{
    CONFORMANCE_CORE_TEST();
    CONFORMANCE_REQUIRE_CAP("drm.clearkey");

    std::unique_ptr<IMediaKeys> mediaKeys = createClearKeyMediaKeys();
    UT_ASSERT_NOT_NULL_FATAL(mediaKeys.get());

    auto client = std::make_shared<RecordingMediaKeysClient>();
    int32_t sessionId = -1;
    UT_ASSERT_EQUAL(mediaKeys->createKeySession(KeySessionType::TEMPORARY, client, sessionId),
                    MediaKeyErrorStatus::OK);
    UT_ASSERT_EQUAL(mediaKeys->generateRequest(sessionId, InitDataType::KEY_IDS, toBytes(kInitDataJson)),
                    MediaKeyErrorStatus::OK);
    UT_ASSERT_TRUE_FATAL(client->waitForLicenseRequest(kCallbackTimeout));
    UT_ASSERT_EQUAL(mediaKeys->updateSession(sessionId, toBytes(kJwkResponse)), MediaKeyErrorStatus::OK);
    UT_ASSERT_TRUE_FATAL(client->waitForKeyStatuses(kCallbackTimeout));

    // The session is valid and key-loaded, so selectKeyId recognises it and
    // answers a defined status. ClearKey (single-key sessions) may not perform a
    // selection, so a specific OK is not required here — only that the valid
    // session is recognised (not BAD_SESSION_ID) and the call does not hang.
    const MediaKeyErrorStatus status = mediaKeys->selectKeyId(sessionId, kKeyId);
    UT_LOG("[keys] selectKeyId(valid session) status=%d", static_cast<int>(status));
    UT_ASSERT_TRUE(status != MediaKeyErrorStatus::BAD_SESSION_ID);

    mediaKeys->closeKeySession(sessionId);
}

/**
 * RC-CORE-KEYS-012 — the media keys client is held as a weak_ptr: destroying the
 * client's owner leaves the session operable (calls still return their status)
 * and no callback is delivered after release. The releasable-client contract is
 * what makes the browser-side ownership model safe.
 */
UT_ADD_TEST(L1KeysSessionTests, ClientHeldWeaklyAndReleasable)
{
    CONFORMANCE_CORE_TEST();
    CONFORMANCE_REQUIRE_CAP("drm.clearkey");

    std::unique_ptr<IMediaKeys> mediaKeys = createClearKeyMediaKeys();
    UT_ASSERT_NOT_NULL_FATAL(mediaKeys.get());

    auto client = std::make_shared<RecordingMediaKeysClient>();
    std::weak_ptr<RecordingMediaKeysClient> weakClient = client;
    int32_t sessionId = -1;
    UT_ASSERT_EQUAL(mediaKeys->createKeySession(KeySessionType::TEMPORARY, client, sessionId),
                    MediaKeyErrorStatus::OK);

    // The client is registered weakly: dropping our strong reference destroys it.
    client.reset();
    UT_ASSERT_TRUE(weakClient.expired());

    // The session remains operable without a live client — generateRequest
    // returns its status (the license-request callback simply has no receiver).
    UT_ASSERT_EQUAL(mediaKeys->generateRequest(sessionId, InitDataType::KEY_IDS, toBytes(kInitDataJson)),
                    MediaKeyErrorStatus::OK);

    UT_ASSERT_EQUAL(mediaKeys->closeKeySession(sessionId), MediaKeyErrorStatus::OK);
}
