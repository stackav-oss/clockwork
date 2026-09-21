// IWYU pragma: private, include "clockwork/logging/onboard/offboard_buffered_reader.hh"
#pragma once

#include "clockwork/logging/onboard/offboard_buffered_reader.hh"

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/onboard/buffered_reader_base.hh"
#include "clockwork/logging/onboard/log_format.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <list>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace clockwork_logging::onboard
{

template <typename Policy>
OffboardBufferedReader<Policy>::OffboardBufferedReader(
  jewels::memory::MemoryResource memory_resource, std::shared_ptr<ChunkReaderFactoryType> chunk_reader_factory)
  : BufferedReaderBase<OffboardBufferedReader<Policy>, Policy>(memory_resource),
    memory_resource_(std::move(memory_resource)),
    chunk_reader_factory_(std::move(chunk_reader_factory))
{
}

template <typename Policy>
[[nodiscard]] LogExpected<void> OffboardBufferedReader<Policy>::open(std::string_view file_path)
{
  if (maybe_file_uri_)
  {
    return jewels::unexpected(LogError::already_open);
  }
  const auto size_result = chunk_reader_factory_->get_size(file_path);
  if (!size_result)
  {
    jewels::log_cerr_error("Failed to get log file size '{}': {}", file_path, size_result.error());
    return jewels::unexpected(size_result.error());
  }
  this->maybe_reader_error_ = std::nullopt;
  maybe_file_uri_ = std::pmr::string{file_path, memory_resource_};
  this->file_size_ = *size_result;
  this->read_window_offset_ = 0U;
  this->first_buffer_offset_ = 0U;
  this->window_size_ = 0U;
  return {};
}

template <typename Policy>
void OffboardBufferedReader<Policy>::close()
{
  if (!maybe_file_uri_)
  {
    return;
  }
  this->maybe_reader_error_ = std::nullopt;
  maybe_file_uri_ = {};
  this->clear_zero_copy_array_spans();
  this->read_window_spans_.clear();
  this->read_window_buffers_.clear();
}

template <typename Policy>
[[nodiscard]] bool OffboardBufferedReader<Policy>::is_open() const noexcept
{
  return static_cast<bool>(maybe_file_uri_);
}

template <typename Policy>
[[nodiscard]] typename OffboardBufferedReader<Policy>::FilesystemType& OffboardBufferedReader<Policy>::get_filesystem()
{
  return chunk_reader_factory_->get_filesystem();
}

template <typename Policy>
[[nodiscard]] LogExpected<std::pmr::list<std::pmr::string>>
OffboardBufferedReader<Policy>::list_log_files(std::string_view log_path)
{
  const auto list_result = chunk_reader_factory_->list_log_files(log_path, log_file_suffix);
  if (!list_result)
  {
    return jewels::unexpected(list_result.error());
  }
  std::pmr::list<std::pmr::string> log_files{memory_resource_};
  for (const auto& log_file_uri : list_result.value())
  {
    log_files.emplace_back(log_file_uri.string());
  }
  return {std::move(log_files)};
}

template <typename Policy>
[[nodiscard]] LogExpected<void>
OffboardBufferedReader<Policy>::read_log_file_trailer(std::string_view file_name, std::span<std::byte> buffer_span)
{
  const auto size_result = chunk_reader_factory_->get_size(file_name);
  if (!size_result)
  {
    return jewels::unexpected(size_result.error());
  }
  if (size_result.value() < buffer_span.size())
  {
    return jewels::unexpected(LogError::empty_log_file);
  }
  const auto read_result =
    chunk_reader_factory_->read_log_file(file_name, size_result.value() - buffer_span.size(), buffer_span);
  if (!read_result)
  {
    return jewels::unexpected(read_result.error());
  }
  if (read_result->size() < buffer_span.size())
  {
    return jewels::unexpected(LogError::empty_log_file);
  }
  return {};
}

template <typename Policy>
[[nodiscard]] LogExpected<void> OffboardBufferedReader<Policy>::grow_window()
{
  if (!maybe_file_uri_)
  {
    return jewels::unexpected{LogError::not_open};
  }
  const auto window_end_offset = this->read_window_offset_ + this->first_buffer_offset_ + this->window_size_;
  if (window_end_offset >= this->file_size_)
  {
    jewels::log_cerr_error("Tried to read past the end of the log file");
    this->maybe_reader_error_ = LogError::failed;
    return jewels::unexpected{*this->maybe_reader_error_};
  }
  auto get_buffer_result = this->buffer_pool_.get_shared_buffer();
  if (!get_buffer_result)
  {
    jewels::log_cerr_error("Failed to get buffer to grow window: {}", get_buffer_result.error());
    this->maybe_reader_error_ = LogError::failed;
    return jewels::unexpected{*this->maybe_reader_error_};
  }
  auto buffer_reference = *std::move(get_buffer_result);
  const auto bytes_to_read = std::min(this->file_size_ - window_end_offset, buffer_reference->size());
  const auto start_time = jewels::time::SteadyClock::now();
  auto read_result = chunk_reader_factory_->read_log_file(
    *maybe_file_uri_, window_end_offset, std::span{buffer_reference->data(), bytes_to_read});
  const auto end_time = jewels::time::SteadyClock::now();
  if (!read_result)
  {
    jewels::log_cerr_error("Unrecoverable error reading log file: {}", read_result.error());
    this->maybe_reader_error_ = read_result.error();
    return jewels::unexpected{*this->maybe_reader_error_};
  }
  if (read_result->size() != bytes_to_read)
  {
    jewels::log_cerr_error("Tried to read past the end of the log file");
    this->maybe_reader_error_ = LogError::failed;
    return jewels::unexpected{*this->maybe_reader_error_};
  }
  if (const auto emplace_result = this->read_window_spans_.emplace_back(buffer_reference->data(), bytes_to_read);
      !emplace_result)
  {
    jewels::log_cerr_error("Failed to add buffer reference to sort window: {}", emplace_result.error());
    this->maybe_reader_error_ = LogError::failed;
    return jewels::unexpected{*this->maybe_reader_error_};
  }
  if (const auto emplace_result = this->read_window_buffers_.emplace_back(std::move(buffer_reference)); !emplace_result)
  {
    jewels::log_cerr_error("Failed to add buffer reference to sort window: {}", emplace_result.error());
    this->maybe_reader_error_ = LogError::failed;
    return jewels::unexpected{*this->maybe_reader_error_};
  }
  this->read_metrics_.byte_count += bytes_to_read;
  ++this->read_metrics_.read_count;
  this->read_metrics_.read_latency += end_time - start_time;
  this->window_size_ += bytes_to_read;
  return {};
}

} // namespace clockwork_logging::onboard
