// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "clockwork/logging/log_error.hh"
#include "jewels/memory/memory_resource.hh"

#include <cstddef>
#include <span>
#include <vector>

namespace clockwork_logging
{

/// Compress data using zstd
/// @param[in] data Data to compress
/// @param[in] memory_resource Memory resource
/// @return Compressed data or LogError on failure
[[nodiscard]] LogExpected<std::pmr::vector<std::byte>>
zstd_compress(std::span<const std::byte> data, jewels::memory::MemoryResource memory_resource);

/// Compress data using zstd
/// @param[in] data Data to compress
/// @param[in] dest_data Destination buffer
/// @return Compressed data or LogError on failure
[[nodiscard]] LogExpected<void> zstd_compress(std::span<const std::byte> data, std::pmr::vector<std::byte>& dest_data);

/// Decompress data using zstd
/// @param[in] data Data to compress
/// @param[in] memory_resource Memory resource
/// @return Decompressed data or LogError on failure
[[nodiscard]] LogExpected<std::pmr::vector<std::byte>>
zstd_decompress(std::span<const std::byte> data, jewels::memory::MemoryResource memory_resource);

/// Decompress data using zstd
/// @param[in] data Data to compress
/// @param[in] dest_data Destination buffer, must be known to be large enough for the decompressed data
/// @return LogError on failure
[[nodiscard]] LogExpected<void> zstd_decompress(std::span<const std::byte> data, std::span<std::byte> dest_data);

} // namespace clockwork_logging
