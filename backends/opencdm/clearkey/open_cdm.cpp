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
 * `_adapter` translation units are unchanged. The session lifecycle is a coherent
 * placeholder (a valid, destructible handle reporting Usable) — full ClearKey
 * session/decrypt lands with the software data path.
 */

#include "opencdm/open_cdm.h"

#include <cstdint>
#include <cstring>
#include <new>
#include <string>

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
        (void)initDataType;
        (void)initData;
        (void)initDataLength;
        (void)CDMData;
        (void)CDMDataLength;
        (void)callbacks;
        (void)userData;
        if (system == nullptr || session == nullptr)
        {
            return ERROR_INVALID_ARG;
        }
        *session = new (std::nothrow) OpenCDMSession{"clearkey-session"};
        return (*session != nullptr) ? ERROR_NONE : ERROR_OUT_OF_MEMORY;
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
        (void)session;
        (void)keyMessage;
        (void)keyLength;
        return ERROR_NONE;
    }

    OpenCDMError opencdm_session_remove(struct OpenCDMSession *session)
    {
        (void)session;
        return ERROR_NONE;
    }

    OpenCDMError opencdm_session_close(struct OpenCDMSession *session)
    {
        (void)session;
        return ERROR_NONE;
    }

    KeyStatus opencdm_session_status(const struct OpenCDMSession *session, const uint8_t keyId[], const uint8_t length)
    {
        (void)session;
        (void)keyId;
        (void)length;
        return Usable;
    }

    const char *opencdm_session_id(const struct OpenCDMSession *session)
    {
        return (session != nullptr) ? session->sessionId.c_str() : nullptr;
    }

    uint32_t opencdm_session_has_key_id(struct OpenCDMSession *session, const uint8_t length, const uint8_t keyId[])
    {
        (void)session;
        (void)length;
        (void)keyId;
        return 1;
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
