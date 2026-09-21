// IWYU pragma: private, include "clockwork/logging/offboard/file_chunk_reader.hh"
#pragma once

#include "clockwork/logging/offboard/file_chunk_reader.hh"

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/filesystem/filesystem.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"

#include <cstddef>
#include <memory_resource>
#include <string_view>
#include <utility>
#include <vector>

namespace clockwork_logging::offboard
{

template <typename FilesystemType>
FileChunkReader<FilesystemType>::FileChunkReader(LogUri file_uri, jewels::memory::MemoryResource memory_resource)
  : file_uri_(std::move(file_uri)), memory_resource_(std::move(memory_resource)), filesystem_(memory_resource_)
{
  filesystem_.set_verbosity(jewels::filesystem::Filesystem::ErrorVerbosity::verbose);
}

template <typename FilesystemType>
[[nodiscard]] LogExpected<jewels::memory::NonNullSharedPtr<FileChunkReader<FilesystemType>>>
FileChunkReader<FilesystemType>::make_shared(
  std::string_view file_uri, const jewels::memory::MemoryResource& memory_resource)
{
  auto maybe_log_uri = LogUri::try_make(file_uri, memory_resource);
  if (!maybe_log_uri || maybe_log_uri->scheme() != LogUriScheme::file)
  {
    jewels::log_cerr_error("Invalid file URI: {}", file_uri);
    return jewels::unexpected(LogError::invalid_log_uri);
  }
  return jewels::memory::
    allocate_shared<FileChunkReader<FilesystemType>, std::pmr::polymorphic_allocator<FileChunkReader<FilesystemType>>>(
      memory_resource, std::move(maybe_log_uri.value()), memory_resource);
}

template <typename FilesystemType>
[[nodiscard]] const LogUri& FileChunkReader<FilesystemType>::file_uri() const noexcept
{
  return file_uri_;
}

template <typename FilesystemType>
[[nodiscard]] LogExpected<void> FileChunkReader<FilesystemType>::open()
{
  if (file_desc_)
  {
    return jewels::unexpected(LogError::already_open);
  }
  if (is_closed_)
  {
    return jewels::unexpected(LogError::already_closed);
  }
  auto open_result = filesystem_.open(file_uri_.path());
  if (!open_result)
  {
    jewels::log_cerr_error("Failed to open {}: {}", file_uri_.path(), open_result.error().message());
    return jewels::unexpected(to_log_error(open_result.error()));
  }
  file_desc_ = std::move(open_result).value();
  return {};
}

template <typename FilesystemType>
[[nodiscard]] LogExpected<size_t> FileChunkReader<FilesystemType>::file_size()
{
  if (is_closed_)
  {
    return jewels::unexpected(LogError::already_closed);
  }
  if (!file_desc_)
  {
    return jewels::unexpected(LogError::not_open);
  }
  const auto size_result = filesystem_.get_size(file_desc_);
  if (!size_result)
  {
    jewels::log_cerr_error("Failed to get size of {}: {}", file_uri_.path(), size_result.error().message());
    return jewels::unexpected(to_log_error(size_result.error()));
  }
  return size_result.value();
}

template <typename FilesystemType>
[[nodiscard]] LogExpected<std::pmr::vector<std::byte>>
FileChunkReader<FilesystemType>::read_chunk(size_t offset, size_t length)
{
  if (is_closed_)
  {
    return jewels::unexpected(LogError::already_closed);
  }
  if (!file_desc_)
  {
    return jewels::unexpected(LogError::not_open);
  }
  std::pmr::vector<std::byte> data(length, memory_resource_);
  const auto read_result = filesystem_.read(file_desc_, offset, data);
  if (!read_result)
  {
    jewels::log_cerr_error("Failed to read from {}: {}", file_uri_.path(), read_result.error().message());
    return jewels::unexpected(to_log_error(read_result.error()));
  }
  if (read_result.value() < data.size())
  {
    return jewels::unexpected(LogError::short_read);
  }
  return data;
}

template <typename FilesystemType>
[[nodiscard]] LogExpected<void> FileChunkReader<FilesystemType>::close()
{
  if (is_closed_)
  {
    return jewels::unexpected(LogError::already_closed);
  }
  if (!file_desc_)
  {
    return jewels::unexpected(LogError::not_open);
  }
  is_closed_ = true;
  const auto close_result = file_desc_.close();
  if (!close_result)
  {
    jewels::log_cerr_error("Failed to close {}: {}", file_uri_.path(), close_result.error().message());
    return jewels::unexpected(to_log_error(close_result.error()));
  }
  return {};
}

template <typename FilesystemType>
[[nodiscard]] FilesystemType& FileChunkReader<FilesystemType>::filesystem() noexcept
{
  return filesystem_;
}

} // namespace clockwork_logging::offboard
