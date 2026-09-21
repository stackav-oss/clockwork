// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/zstd_helper.hh"

#include "jewels/std/expected.hh"

#include <zstd.h>

#include <memory_resource>
#include <utility>

namespace clockwork_logging
{

namespace
{

/// ZSTD compression level
constexpr auto compression_level = ZSTD_CLEVEL_DEFAULT;

} // namespace

[[nodiscard]] LogExpected<std::pmr::vector<std::byte>>
zstd_compress(std::span<const std::byte> data, jewels::memory::MemoryResource memory_resource)
{
  std::pmr::vector<std::byte> dest_data(ZSTD_compressBound(data.size()), memory_resource);
  const auto result = zstd_compress(data, dest_data);

  if (!result)
  {
    return jewels::unexpected(result.error());
  }

  return {std::move(dest_data)};
}

[[nodiscard]] LogExpected<void> zstd_compress(std::span<const std::byte> data, std::pmr::vector<std::byte>& dest_data)
{
  dest_data.resize(ZSTD_compressBound(data.size()));
  const auto zstd_result =
    ZSTD_compress(dest_data.data(), dest_data.size(), data.data(), data.size(), compression_level);
  if (ZSTD_isError(zstd_result) != 0U)
  {
    return jewels::unexpected(LogError::compression_failure);
  }
  dest_data.resize(zstd_result);
  return {};
}

[[nodiscard]] LogExpected<std::pmr::vector<std::byte>>
zstd_decompress(std::span<const std::byte> data, jewels::memory::MemoryResource memory_resource)
{
  const auto decompressed_size = ZSTD_getFrameContentSize(data.data(), data.size());
  if (decompressed_size == ZSTD_CONTENTSIZE_UNKNOWN || decompressed_size == ZSTD_CONTENTSIZE_ERROR)
  {
    return jewels::unexpected(LogError::decompression_failure);
  }
  std::pmr::vector<std::byte> dest_data(decompressed_size, memory_resource);
  const auto zstd_result = ZSTD_decompress(dest_data.data(), dest_data.size(), data.data(), data.size());
  if (ZSTD_isError(zstd_result) != 0U)
  {
    return jewels::unexpected(LogError::decompression_failure);
  }
  return {std::move(dest_data)};
}

[[nodiscard]] LogExpected<void> zstd_decompress(std::span<const std::byte> data, std::span<std::byte> dest_data)
{
  const auto decompressed_size = ZSTD_getFrameContentSize(data.data(), data.size());
  if (decompressed_size == ZSTD_CONTENTSIZE_UNKNOWN || decompressed_size == ZSTD_CONTENTSIZE_ERROR)
  {
    return jewels::unexpected(LogError::decompression_failure);
  }
  if (dest_data.size() < decompressed_size)
  {
    return jewels::unexpected(LogError::decompression_failure);
  }
  const auto zstd_result = ZSTD_decompress(dest_data.data(), dest_data.size(), data.data(), data.size());
  if (ZSTD_isError(zstd_result) != 0U)
  {
    return jewels::unexpected(LogError::decompression_failure);
  }
  return {};
}

} // namespace clockwork_logging
