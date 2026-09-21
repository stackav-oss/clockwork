// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/offboard/s3_chunk_reader.hh"

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"

#include <cstddef>
#include <memory>
#include <memory_resource>
#include <string_view>
#include <utility>
#include <vector>

namespace clockwork_logging::offboard
{

S3ChunkReader::S3ChunkReader(
  jewels::memory::MemoryResource memory_resource,
  LogUri file_uri,
  const jewels::memory::NonNullSharedPtr<S3UtilsInterface>& s3_utils_ptr)
  : memory_resource_(std::move(memory_resource)), file_uri_(std::move(file_uri)), s3_utils_ptr_(s3_utils_ptr)
{
}

[[nodiscard]] LogExpected<jewels::memory::NonNullSharedPtr<S3ChunkReader>> S3ChunkReader::make_shared(
  const jewels::memory::MemoryResource& memory_resource,
  std::string_view file_uri,
  const jewels::memory::NonNullSharedPtr<S3UtilsInterface>& s3_utils_ptr)
{
  auto maybe_log_uri = LogUri::try_make(file_uri, memory_resource);
  if (!maybe_log_uri || maybe_log_uri->scheme() != LogUriScheme::s3)
  {
    jewels::log_cerr_error("Invalid S3 URI: {}", file_uri);
    return jewels::unexpected(LogError::invalid_log_uri);
  }
  return jewels::memory::allocate_shared<S3ChunkReader, std::pmr::polymorphic_allocator<S3ChunkReader>>(
    memory_resource, memory_resource, std::move(maybe_log_uri.value()), s3_utils_ptr);
}

[[nodiscard]] const LogUri& S3ChunkReader::file_uri() const noexcept
{
  return file_uri_;
}

[[nodiscard]] LogExpected<void> S3ChunkReader::open()
{
  if (is_open_)
  {
    return jewels::unexpected(LogError::already_open);
  }
  if (is_closed_)
  {
    return jewels::unexpected(LogError::already_closed);
  }
  is_open_ = true;
  return {};
}

[[nodiscard]] LogExpected<size_t> S3ChunkReader::file_size()
{
  if (!is_open_)
  {
    return jewels::unexpected(LogError::not_open);
  }
  return s3_utils_ptr_->get_object_size(file_uri_);
}

[[nodiscard]] LogExpected<std::pmr::vector<std::byte>> S3ChunkReader::read_chunk(size_t offset, size_t length)
{
  if (!is_open_)
  {
    return jewels::unexpected(LogError::not_open);
  }
  return s3_utils_ptr_->get_object(file_uri_, offset, length);
}

[[nodiscard]] LogExpected<void> S3ChunkReader::close()
{
  if (is_closed_)
  {
    return jewels::unexpected(LogError::already_closed);
  }
  if (!is_open_)
  {
    return jewels::unexpected(LogError::not_open);
  }
  is_open_ = false;
  is_closed_ = true;
  return {};
}

} // namespace clockwork_logging::offboard
