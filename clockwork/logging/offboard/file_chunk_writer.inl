// IWYU pragma: private, include "clockwork/logging/offboard/file_chunk_writer.hh"
#pragma once

#include "clockwork/logging/offboard/file_chunk_writer.hh"

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/offboard/chunk_writer.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/filesystem/filesystem.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"

#include <chrono>
#include <cstddef>
#include <fcntl.h>
#include <list>
#include <memory_resource>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace clockwork_logging::offboard
{

template <typename FilesystemType>
FileChunkWriter<FilesystemType>::FileChunkWriter(LogUri file_uri, jewels::memory::MemoryResource memory_resource)
  : file_uri_(std::move(file_uri)), filesystem_(memory_resource)
{
  filesystem_.set_verbosity(jewels::filesystem::Filesystem::ErrorVerbosity::verbose);
}

template <typename FilesystemType>
FileChunkWriter<FilesystemType>::~FileChunkWriter()
{
  if (file_desc_)
  {
    jewels::log_cerr_warn("chunk file writer for {} not closed cleanly", file_uri_.string());
  }
}

template <typename FilesystemType>
[[nodiscard]] LogExpected<jewels::memory::NonNullSharedPtr<FileChunkWriter<FilesystemType>>>
FileChunkWriter<FilesystemType>::make_shared(
  std::string_view file_uri, const jewels::memory::MemoryResource& memory_resource)
{
  auto maybe_log_uri = LogUri::try_make(file_uri, memory_resource);
  if (!maybe_log_uri || maybe_log_uri->scheme() != LogUriScheme::file)
  {
    jewels::log_cerr_error("Invalid file URI: {}", file_uri);
    return jewels::unexpected(LogError::invalid_log_uri);
  }
  return jewels::memory::
    allocate_shared<FileChunkWriter<FilesystemType>, std::pmr::polymorphic_allocator<FileChunkWriter<FilesystemType>>>(
      memory_resource, std::move(maybe_log_uri.value()), memory_resource);
}

template <typename FilesystemType>
[[nodiscard]] const LogUri& FileChunkWriter<FilesystemType>::file_uri() const noexcept
{
  return file_uri_;
}

template <typename FilesystemType>
[[nodiscard]] LogExpected<void> FileChunkWriter<FilesystemType>::open()
{
  if (file_desc_)
  {
    return jewels::unexpected(LogError::already_open);
  }
  if (is_closed_)
  {
    return jewels::unexpected(LogError::already_closed);
  }
  auto open_result = filesystem_.open(file_uri_.path(), O_CREAT | O_EXCL | O_WRONLY);
  if (!open_result)
  {
    jewels::log_cerr_error("Failed to open {}: {}", file_uri_.path(), open_result.error().message());
    return jewels::unexpected(to_log_error(open_result.error()));
  }
  file_desc_ = std::move(open_result).value();
  return {};
}

template <typename FilesystemType>
[[nodiscard]] LogExpected<size_t> FileChunkWriter<FilesystemType>::get_file_size() const
{
  if (is_closed_)
  {
    return jewels::unexpected(LogError::already_closed);
  }
  if (!file_desc_)
  {
    return jewels::unexpected(LogError::not_open);
  }
  const std::lock_guard guard{mutex_};
  return current_offset_;
}

template <typename FilesystemType>
[[nodiscard]] LogExpected<size_t> FileChunkWriter<FilesystemType>::write_chunk(std::pmr::vector<std::byte> data)
{
  if (is_closed_)
  {
    return jewels::unexpected(LogError::already_closed);
  }
  if (!file_desc_)
  {
    return jewels::unexpected(LogError::not_open);
  }
  size_t chunk_offset{};
  {
    const std::lock_guard guard{mutex_};
    chunk_offset = current_offset_;
    current_offset_ += data.size();
  }
  const auto start_time = jewels::time::SteadyClock::now();
  const auto write_result = filesystem_.write(file_desc_, chunk_offset, data);
  if (!write_result)
  {
    jewels::log_cerr_error("Failed to write to {}: {}", file_uri_.path(), write_result.error().message());
    return jewels::unexpected(to_log_error(write_result.error()));
  }
  const auto end_time = jewels::time::SteadyClock::now();
  const std::lock_guard guard{mutex_};
  write_metrics_.byte_count += data.size();
  ++write_metrics_.write_count;
  write_metrics_.write_latency += end_time - start_time;
  return chunk_offset;
}

template <typename FilesystemType>
[[nodiscard]] LogExpected<ChunkWriter::WriteMetrics> FileChunkWriter<FilesystemType>::close()
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
  if (const auto close_result = file_desc_.close(); !close_result)
  {
    jewels::log_cerr_error("Failed to close {}: {}", file_uri_.path(), close_result.error().message());
    return jewels::unexpected(to_log_error(close_result.error()));
  }
  return {write_metrics_};
}

template <typename FilesystemType>
[[nodiscard]] FilesystemType& FileChunkWriter<FilesystemType>::filesystem() noexcept
{
  return filesystem_;
}

} // namespace clockwork_logging::offboard
