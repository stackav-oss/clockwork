// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/offboard/s3_chunk_writer.hh"

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/offboard/log_format.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "clockwork/logging/offboard/s3_utils.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"

#include <chrono>
#include <cstddef>
#include <cstring>
#include <deque>
#include <initializer_list>
#include <memory>
#include <memory_resource>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace clockwork_logging::offboard
{

S3ChunkWriter::S3ChunkWriter(
  jewels::memory::MemoryResource memory_resource,
  LogUri file_uri,
  jewels::memory::NonNullSharedPtr<Aws::S3::S3Client> s3_client_ptr)
  : memory_resource_(std::move(memory_resource)),
    file_uri_(std::move(file_uri)),
    s3_client_ptr_(std::move(s3_client_ptr)),
    pending_buffer_(memory_resource_)
{
}

S3ChunkWriter::~S3ChunkWriter()
{
  if (is_open_)
  {
    jewels::log_cerr_warn("chunk file writer for {} not closed cleanly", file_uri_.string());
  }
}

[[nodiscard]] LogExpected<jewels::memory::NonNullSharedPtr<S3ChunkWriter>> S3ChunkWriter::make_shared(
  const jewels::memory::MemoryResource& memory_resource,
  std::string_view file_uri,
  const jewels::memory::NonNullSharedPtr<Aws::S3::S3Client>& s3_client_ptr)
{
  auto maybe_log_uri = LogUri::try_make(file_uri, memory_resource);
  if (!maybe_log_uri || maybe_log_uri->scheme() != LogUriScheme::s3)
  {
    jewels::log_cerr_error("Invalid S3 URI: {}", file_uri);
    return jewels::unexpected(LogError::invalid_log_uri);
  }
  return jewels::memory::allocate_shared<S3ChunkWriter, std::pmr::polymorphic_allocator<S3ChunkWriter>>(
    memory_resource, memory_resource, std::move(maybe_log_uri.value()), s3_client_ptr);
}

[[nodiscard]] const LogUri& S3ChunkWriter::file_uri() const noexcept
{
  return file_uri_;
}

[[nodiscard]] LogExpected<void> S3ChunkWriter::open()
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

[[nodiscard]] LogExpected<size_t> S3ChunkWriter::get_file_size() const
{
  if (is_closed_)
  {
    return jewels::unexpected(LogError::already_closed);
  }
  if (!is_open_)
  {
    return jewels::unexpected(LogError::not_open);
  }
  const std::lock_guard guard{mutex_};
  return next_chunk_offset_;
}

[[nodiscard]] LogExpected<size_t> S3ChunkWriter::write_chunk(std::pmr::vector<std::byte> data)
{
  if (is_closed_)
  {
    return jewels::unexpected(LogError::already_closed);
  }
  if (!is_open_)
  {
    return jewels::unexpected(LogError::not_open);
  }
  std::unique_lock guard{mutex_};
  const auto chunk_offset = next_chunk_offset_;
  next_chunk_offset_ += data.size();
  pending_buffer_bytes_ += data.size();
  if (pending_buffer_.empty())
  {
    pending_buffer_ = std::move(data);
  }
  else
  {
    const auto prev_size = pending_buffer_.size();
    pending_buffer_.resize(prev_size + data.size());
    std::memcpy(&pending_buffer_.at(prev_size), data.data(), data.size());
  }
  if (pending_buffer_bytes_ >= target_file_chunk_size)
  {
    if (upload_id_.empty())
    {
      auto create_result = s3_create_multipart_upload(memory_resource_, *s3_client_ptr_, file_uri_);
      if (!create_result)
      {
        return jewels::unexpected(create_result.error());
      }
      upload_id_ = std::move(create_result).value();
    }
    if (const auto upload_result = upload_pending_buffer(guard); !upload_result)
    {
      return jewels::unexpected(upload_result.error());
    }
  }

  return chunk_offset;
}

[[nodiscard]] LogExpected<ChunkWriter::WriteMetrics> S3ChunkWriter::close()
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
  std::unique_lock guard{mutex_};
  if (!pending_buffer_.empty())
  {
    if (!upload_id_.empty())
    {
      if (const auto upload_result = upload_pending_buffer(guard); !upload_result)
      {
        return jewels::unexpected(upload_result.error());
      }
    }
    else
    {
      const auto data_size = pending_buffer_.size();
      const auto start_time = jewels::time::SteadyClock::now();
      if (const auto put_result = s3_put_object(
            *s3_client_ptr_,
            file_uri_,
            std::pmr::vector<std::pmr::vector<std::byte>>{{std::move(pending_buffer_)}, memory_resource_});
          !put_result)
      {
        return jewels::unexpected(put_result.error());
      }
      const auto end_time = jewels::time::SteadyClock::now();
      write_metrics_.byte_count += data_size;
      ++write_metrics_.write_count;
      write_metrics_.write_latency += end_time - start_time;
    }
  }
  if (!upload_id_.empty())
  {
    if (const auto complete_result =
          s3_complete_multipart_upload(*s3_client_ptr_, file_uri_, upload_id_, completed_parts_);
        !complete_result)
    {
      return jewels::unexpected(complete_result.error());
    }
  }
  return {write_metrics_};
}

[[nodiscard]] LogExpected<void> S3ChunkWriter::upload_pending_buffer(std::unique_lock<std::mutex>& guard)
{
  if (pending_buffer_.empty())
  {
    return {};
  }
  const auto data_size = pending_buffer_.size();
  auto buffers_ptr = std::make_shared<std::pmr::vector<std::pmr::vector<std::byte>>>(
    std::pmr::vector<std::pmr::vector<std::byte>>{{std::move(pending_buffer_)}, memory_resource_});
  const auto part_number = next_part_number_;
  ++next_part_number_;
  pending_buffer_ = {};
  pending_buffer_bytes_ = 0U;
  guard.unlock();
  const auto start_time = jewels::time::SteadyClock::now();
  auto upload_result = s3_upload_part(*s3_client_ptr_, file_uri_, upload_id_, part_number, std::move(*buffers_ptr));
  const auto end_time = jewels::time::SteadyClock::now();
  guard.lock();
  if (!upload_result)
  {
    return jewels::unexpected(upload_result.error());
  }
  write_metrics_.byte_count += data_size;
  ++write_metrics_.write_count;
  write_metrics_.write_latency += end_time - start_time;
  completed_parts_.push_back(std::move(upload_result.value()));
  return {};
}

} // namespace clockwork_logging::offboard
