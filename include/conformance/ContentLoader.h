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

#ifndef RIALTO_CONFORMANCE_CONTENT_LOADER_H_
#define RIALTO_CONFORMANCE_CONTENT_LOADER_H_

/**
 * @file ContentLoader.h
 *
 * Elementary-stream feed for both surfaces (§3 req 5 / §7.1). Cases declare an
 * asset dependency by `id`; the loader resolves it against assets/manifest.yaml
 * and returns a handle to the real elementary stream on disk. Real content
 * flows through the MSE pipeline (Surface A) and the native data path
 * (Surface B) — this is specifically NOT a mock.
 *
 * Assets are fetched, not vendored: at the start of a run the loader downloads
 * any manifest asset that the selected cases need into assets/cache/ (verified
 * against the manifest checksum), then proceeds.
 */

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rialto::conformance
{
/**
 * A single elementary-stream asset, resolved from assets/manifest.yaml.
 */
struct ContentAsset
{
    std::string id;        ///< manifest id, e.g. "h264-1080p-cmaf"
    std::string codec;     ///< e.g. "h264", "hevc", "aac", "eac3"
    std::string keySystem; ///< "" if clear, else e.g. "com.widevine.alpha"
    std::string localPath; ///< path under assets/cache/ once fetched
    std::string mimeType;  ///< MSE/native mime, e.g. "video/h264"
};

/**
 * Resolves and feeds elementary streams for the cases that need them.
 *
 * The implementation reads assets/manifest.yaml, ensures each requested asset
 * is present in assets/cache/ (downloading + checksum-verifying on demand), and
 * hands back a buffer/segment view the surface fixtures push through Rialto.
 */
class ContentLoader
{
public:
    /**
     * @brief Resolve an asset by manifest id, fetching it if missing.
     *
     * @param id  manifest id of the asset.
     * @retval the resolved asset; localPath is populated and verified.
     * @throws std::runtime_error if the id is unknown or the checksum fails.
     */
    static ContentAsset require(const std::string &id);

    /**
     * @brief Read a contiguous chunk of an asset's elementary stream.
     *
     * @param asset      a previously resolved asset.
     * @param offset     byte offset into the stream.
     * @param maxBytes   maximum bytes to read.
     * @retval the bytes read (size may be < maxBytes at EOS).
     */
    static std::vector<uint8_t> readChunk(const ContentAsset &asset, size_t offset, size_t maxBytes);
};

} // namespace rialto::conformance

#endif // RIALTO_CONFORMANCE_CONTENT_LOADER_H_
