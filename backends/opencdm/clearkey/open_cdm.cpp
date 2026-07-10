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
 * @file open_cdm.cpp
 *
 * ClearKey OpenCDM backend for the Linux software platform.
 *
 * Rialto's NATIVE_BUILD links a `libocdm` built from the `stubs/opencdm` sources.
 * The upstream stub answers every OpenCDM query permissively — `is_type_supported`
 * returns success for any string and `create_system` always fails — so on the
 * software platform the DRM capability surface is not conformant: an unsupported
 * key system is reported supported and no version is obtainable. This file is the
 * suite-owned drop-in replacement for `stubs/opencdm/open_cdm.cpp`
 * (build-rialto.sh overlays it when RIALTO_OCDM_BACKEND=clearkey, the default),
 * giving a real, if minimal, W3C ClearKey CDM:
 *
 *   - the ClearKey key system ("org.w3.clearkey") is supported and reports a
 *     version; every other key system is rejected (ERROR_KEYSYSTEM_NOT_SUPPORTED
 *     from is_type_supported, nullptr from create_system);
 *   - ClearKey carries no service certificate, so server-certificate support is
 *     reported false.
 *
 * This makes IMediaKeysCapabilities conformant in software CI (the unsupported/
 * version/server-certificate contracts hold) and lets ClearKey capability cases
 * run against a genuine backend rather than self-skip.
 *
 * The interface is the stable OpenCDM C ABI, so a real CDM can be swapped in
 * behind the same `libocdm` later (RIALTO_OCDM_BACKEND selects the backend). Only
 * the functions upstream's `open_cdm.cpp` defines live here; the `_ext` and
 * `_adapter` translation units are unchanged.
 *
 * The session lifecycle implements the W3C ClearKey exchange
 * (https://www.w3.org/TR/encrypted-media/#clear-key-request-format):
 * construct_session parses `keyids` init data ({"kids":["<b64url>",...]}), builds
 * the ClearKey license-request JSON and delivers it through
 * process_challenge_callback (the Rialto wrapper's onProcessChallenge enqueues,
 * so a synchronous fire is safe); session_update parses the JWK response
 * ({"keys":[{"kty":"oct","kid":"<b64url>","k":"<b64url>"},...]}), stores the
 * keys, and fires key_update_callback per key then keys_updated_callback;
 * session_status / has_key_id answer from the stored key set.
 */

#include "opencdm/open_cdm.h"

#include <cstdint>
#include <cstring>
#include <map>
#include <new>
#include <string>
#include <vector>

namespace
{
// The W3C Clear Key key system (https://www.w3.org/TR/encrypted-media/#clear-key).
constexpr const char *kClearKeySystem = "org.w3.clearkey";
// Reported by opencdm_system_get_version. Must be non-empty to satisfy the
// version contract; ClearKey has no vendor DRM version, so this identifies the
// suite's software CDM.
constexpr const char *kClearKeyVersion = "ClearKey-1.0";

bool isClearKeySystem(const char *keySystem)
{
    return keySystem != nullptr && std::string{keySystem} == kClearKeySystem;
}

// --- base64url (RFC 4648 §5, no padding) — the encoding W3C ClearKey uses -----

std::string base64UrlEncode(const std::vector<uint8_t> &data)
{
    static const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    size_t i = 0;
    while (i + 2 < data.size())
    {
        uint32_t n = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
        out += kAlphabet[(n >> 18) & 63];
        out += kAlphabet[(n >> 12) & 63];
        out += kAlphabet[(n >> 6) & 63];
        out += kAlphabet[n & 63];
        i += 3;
    }
    if (i + 1 == data.size())
    {
        uint32_t n = data[i] << 16;
        out += kAlphabet[(n >> 18) & 63];
        out += kAlphabet[(n >> 12) & 63];
    }
    else if (i + 2 == data.size())
    {
        uint32_t n = (data[i] << 16) | (data[i + 1] << 8);
        out += kAlphabet[(n >> 18) & 63];
        out += kAlphabet[(n >> 12) & 63];
        out += kAlphabet[(n >> 6) & 63];
    }
    return out;
}

int base64UrlValue(char c)
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '-')
        return 62;
    if (c == '_')
        return 63;
    return -1;
}

bool base64UrlDecode(const std::string &text, std::vector<uint8_t> &out)
{
    out.clear();
    uint32_t acc = 0;
    int bits = 0;
    for (char c : text)
    {
        if (c == '=')
            continue; // tolerate padded input
        const int v = base64UrlValue(c);
        if (v < 0)
            return false;
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((acc >> bits) & 0xFF));
        }
    }
    return true;
}

// --- minimal JSON field scanning (enough for the two fixed ClearKey shapes) ---

/// Collect every string value of field @p key ("kid", "k", or the "kids" array
/// members) from @p json. Not a general parser: scans for `"<key>"` and then
/// takes the following quoted string(s) — array-valued fields take all members.
std::vector<std::string> jsonStringValues(const std::string &json, const std::string &key)
{
    std::vector<std::string> values;
    const std::string needle = "\"" + key + "\"";
    size_t pos = 0;
    while ((pos = json.find(needle, pos)) != std::string::npos)
    {
        pos += needle.size();
        // Skip to the ':' then the value.
        size_t colon = json.find(':', pos);
        if (colon == std::string::npos)
            break;
        size_t cursor = colon + 1;
        while (cursor < json.size() && (json[cursor] == ' ' || json[cursor] == '\t' || json[cursor] == '\n'))
            ++cursor;
        if (cursor < json.size() && json[cursor] == '[')
        {
            // Array value: take every quoted string until the closing bracket.
            const size_t end = json.find(']', cursor);
            size_t q = cursor;
            while (true)
            {
                const size_t open = json.find('"', q);
                if (open == std::string::npos || (end != std::string::npos && open > end))
                    break;
                const size_t close = json.find('"', open + 1);
                if (close == std::string::npos)
                    break;
                values.push_back(json.substr(open + 1, close - open - 1));
                q = close + 1;
            }
        }
        else if (cursor < json.size() && json[cursor] == '"')
        {
            const size_t close = json.find('"', cursor + 1);
            if (close == std::string::npos)
                break;
            values.push_back(json.substr(cursor + 1, close - cursor - 1));
            pos = close + 1;
            continue;
        }
    }
    return values;
}
} // namespace

// Complete the OpenCDM opaque handle types for this translation unit. The `_ext`
// and `_adapter` units only pass these pointers through, so completing them here
// (the opaque-handle idiom) does not conflict with them.
struct OpenCDMSystem
{
    std::string keySystem;
};

struct OpenCDMSession
{
    std::string sessionId;
    OpenCDMSessionCallbacks *callbacks = nullptr;
    void *userData = nullptr;
    std::vector<std::vector<uint8_t>> requestedKeyIds;         ///< from the init data
    std::map<std::vector<uint8_t>, std::vector<uint8_t>> keys; ///< kid -> key (from the JWK response)
    bool closed = false;
};

extern "C"
{
    OpenCDMSystem *opencdm_create_system(const char keySystem[])
    {
        if (!isClearKeySystem(keySystem))
        {
            return nullptr;
        }
        return new (std::nothrow) OpenCDMSystem{keySystem};
    }

    OpenCDMError opencdm_destruct_system(struct OpenCDMSystem *system)
    {
        delete system;
        return ERROR_NONE;
    }

    OpenCDMError opencdm_is_type_supported(const char keySystem[], const char mimeType[])
    {
        (void)mimeType; // mimeType is ignored by OpenCDM (see header contract).
        return isClearKeySystem(keySystem) ? ERROR_NONE : ERROR_KEYSYSTEM_NOT_SUPPORTED;
    }

    OpenCDMError opencdm_system_get_version(struct OpenCDMSystem *system, char versionStr[])
    {
        if (system == nullptr || versionStr == nullptr)
        {
            return ERROR_INVALID_ARG;
        }
        // Caller supplies a >= 64-byte buffer (per the header contract).
        std::strncpy(versionStr, kClearKeyVersion, 63);
        versionStr[63] = '\0';
        return ERROR_NONE;
    }

    OpenCDMError opencdm_system_get_drm_time(struct OpenCDMSystem *system, uint64_t *time)
    {
        (void)system;
        if (time != nullptr)
        {
            *time = 0; // ClearKey keeps no DRM clock; report epoch.
        }
        return ERROR_NONE;
    }

    OpenCDMBool opencdm_system_supports_server_certificate(struct OpenCDMSystem *system)
    {
        (void)system;
        // ClearKey exchanges keys in the clear; it has no service certificate.
        return OpenCDMBool::OPENCDM_BOOL_FALSE;
    }

    OpenCDMError opencdm_get_metric_system_data(struct OpenCDMSystem *system, uint32_t *bufferLength, uint8_t *buffer)
    {
        (void)system;
        (void)buffer;
        if (bufferLength != nullptr)
        {
            *bufferLength = 0; // ClearKey exposes no metrics.
        }
        return ERROR_NONE;
    }

    OpenCDMError opencdm_construct_session(struct OpenCDMSystem *system, const LicenseType licenseType,
                                           const char initDataType[], const uint8_t initData[],
                                           const uint16_t initDataLength, const uint8_t CDMData[],
                                           const uint16_t CDMDataLength, OpenCDMSessionCallbacks *callbacks,
                                           void *userData, OpenCDMSession **session)
    {
        (void)licenseType;
        (void)CDMData;
        (void)CDMDataLength;
        if (system == nullptr || session == nullptr)
        {
            return ERROR_INVALID_ARG;
        }

        // ClearKey init data: the W3C "keyids" JSON ({"kids":["<b64url>",...]}).
        // "cenc" (PSSH) and "webm" carry key ids the same tests do not use; an
        // unparseable/empty kid set fails construction (the CDM cannot build a
        // license request for no keys).
        if (initDataType == nullptr || std::string{initDataType} != "keyids")
        {
            return ERROR_FAIL;
        }
        const std::string initJson{reinterpret_cast<const char *>(initData),
                                   reinterpret_cast<const char *>(initData) + initDataLength};
        std::vector<std::vector<uint8_t>> kids;
        for (const std::string &kidB64 : jsonStringValues(initJson, "kids"))
        {
            std::vector<uint8_t> kid;
            if (base64UrlDecode(kidB64, kid) && !kid.empty())
            {
                kids.push_back(std::move(kid));
            }
        }
        if (kids.empty())
        {
            return ERROR_FAIL;
        }

        static uint32_t sessionCounter = 0;
        auto *newSession = new (std::nothrow) OpenCDMSession{};
        if (newSession == nullptr)
        {
            return ERROR_OUT_OF_MEMORY;
        }
        newSession->sessionId = "clearkey-session-" + std::to_string(++sessionCounter);
        newSession->callbacks = callbacks;
        newSession->userData = userData;
        newSession->requestedKeyIds = std::move(kids);
        *session = newSession;

        // Deliver the W3C ClearKey license request through the challenge
        // callback. Synchronous delivery is safe: the Rialto wrapper's
        // onProcessChallenge handler only enqueues a task.
        if (callbacks != nullptr && callbacks->process_challenge_callback != nullptr)
        {
            std::string request{"{\"kids\":["};
            for (size_t i = 0; i < newSession->requestedKeyIds.size(); ++i)
            {
                if (i > 0)
                    request += ",";
                request += "\"" + base64UrlEncode(newSession->requestedKeyIds[i]) + "\"";
            }
            request += "],\"type\":\"temporary\"}";
            callbacks->process_challenge_callback(newSession, userData, /*url*/ "",
                                                  reinterpret_cast<const uint8_t *>(request.data()),
                                                  static_cast<uint16_t>(request.size()));
        }
        return ERROR_NONE;
    }

    OpenCDMError opencdm_destruct_session(struct OpenCDMSession *session)
    {
        delete session;
        return ERROR_NONE;
    }

    OpenCDMError opencdm_session_load(struct OpenCDMSession *session)
    {
        (void)session;
        return ERROR_NONE;
    }

    OpenCDMError opencdm_session_update(struct OpenCDMSession *session, const uint8_t keyMessage[],
                                        const uint16_t keyLength)
    {
        if (session == nullptr || keyMessage == nullptr || keyLength == 0)
        {
            return ERROR_INVALID_ARG;
        }
        if (session->closed)
        {
            return ERROR_INVALID_SESSION;
        }

        // W3C ClearKey license response: a JWK set,
        // {"keys":[{"kty":"oct","kid":"<b64url>","k":"<b64url>"},...]}. The kid/k
        // fields pair up positionally (the scanner returns them in order).
        const std::string response{reinterpret_cast<const char *>(keyMessage),
                                   reinterpret_cast<const char *>(keyMessage) + keyLength};
        const std::vector<std::string> kids = jsonStringValues(response, "kid");
        const std::vector<std::string> keys = jsonStringValues(response, "k");
        if (kids.empty() || kids.size() != keys.size())
        {
            return ERROR_FAIL;
        }

        std::vector<std::vector<uint8_t>> updatedKids;
        for (size_t i = 0; i < kids.size(); ++i)
        {
            std::vector<uint8_t> kid;
            std::vector<uint8_t> key;
            if (!base64UrlDecode(kids[i], kid) || !base64UrlDecode(keys[i], key) || kid.empty() || key.empty())
            {
                return ERROR_FAIL;
            }
            session->keys[kid] = key;
            updatedKids.push_back(std::move(kid));
        }

        // Per-key status updates then the batch-complete signal — the sequence
        // the Rialto wrapper turns into onKeyStatusesChanged.
        if (session->callbacks != nullptr)
        {
            if (session->callbacks->key_update_callback != nullptr)
            {
                for (const auto &kid : updatedKids)
                {
                    session->callbacks->key_update_callback(session, session->userData, kid.data(),
                                                            static_cast<uint8_t>(kid.size()));
                }
            }
            if (session->callbacks->keys_updated_callback != nullptr)
            {
                session->callbacks->keys_updated_callback(session, session->userData);
            }
        }
        return ERROR_NONE;
    }

    OpenCDMError opencdm_session_remove(struct OpenCDMSession *session)
    {
        if (session == nullptr)
        {
            return ERROR_INVALID_ARG;
        }
        // Removing the session discards its keys (they are no longer usable).
        session->keys.clear();
        return ERROR_NONE;
    }

    OpenCDMError opencdm_session_close(struct OpenCDMSession *session)
    {
        if (session == nullptr)
        {
            return ERROR_INVALID_ARG;
        }
        session->closed = true;
        return ERROR_NONE;
    }

    KeyStatus opencdm_session_status(const struct OpenCDMSession *session, const uint8_t keyId[], const uint8_t length)
    {
        if (session == nullptr || keyId == nullptr || length == 0)
        {
            return InternalError;
        }
        const std::vector<uint8_t> kid{keyId, keyId + length};
        // Usable once the JWK response supplied the key; pending until then.
        return (session->keys.count(kid) != 0) ? Usable : StatusPending;
    }

    const char *opencdm_session_id(const struct OpenCDMSession *session)
    {
        return (session != nullptr) ? session->sessionId.c_str() : nullptr;
    }

    uint32_t opencdm_session_has_key_id(struct OpenCDMSession *session, const uint8_t length, const uint8_t keyId[])
    {
        if (session == nullptr || keyId == nullptr || length == 0)
        {
            return 0;
        }
        const std::vector<uint8_t> kid{keyId, keyId + length};
        return (session->keys.count(kid) != 0) ? 1 : 0;
    }

    OpenCDMError opencdm_session_set_drm_header(struct OpenCDMSession *opencdmSession, const uint8_t drmHeader[],
                                                uint32_t drmHeaderSize)
    {
        (void)opencdmSession;
        (void)drmHeader;
        (void)drmHeaderSize;
        return ERROR_NONE;
    }

    OpenCDMError opencdm_session_system_error(const struct OpenCDMSession *session)
    {
        (void)session;
        return ERROR_NONE;
    }
}
