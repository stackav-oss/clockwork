// IWYU pragma: private, include "clockwork/logging/onboard/buffered_reader.hh"
#pragma once

#include "clockwork/logging/onboard/buffered_reader.hh"

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/onboard/buffered_reader_base.hh"
#include "clockwork/logging/onboard/log_format.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/filesystem/filesystem.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <dirent.h>
#include <list>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace clockwork_logging::onboard
{

template <typename Policy>
BufferedReader<Policy>::BufferedReader(jewels::memory::MemoryResource memory_resource)
  : BufferedReaderBase<BufferedReader<Policy>, Policy>(memory_resource),
    memory_resource_(std::move(memory_resource)),
    kits_fs_(memory_resource_)
{
  kits_fs_.set_verbosity(jewels::filesystem::Filesystem::ErrorVerbosity::verbose);
}

template <typename Policy>
[[nodiscard]] LogExpected<void> BufferedReader<Policy>::open(std::string_view file_path)
{
  if (file_desc_)
  {
    return jewels::unexpected(LogError::already_open);
  }
  auto open_result = kits_fs_.open(file_path);
  if (!open_result)
  {
    jewels::log_cerr_error("Failed to open log file '{}': {}", file_path, open_result.error().message());
    return jewels::unexpected(to_log_error(open_result.error()));
  }
  const auto stat_result = kits_fs_.get_size(*open_result);
  if (!stat_result)
  {
    jewels::log_cerr_error("Failed to get log file size '{}': {}", file_path, stat_result.error().message());
    return jewels::unexpected(to_log_error(stat_result.error()));
  }
  this->maybe_reader_error_ = std::nullopt;
  file_desc_ = *std::move(open_result);
  this->file_size_ = *stat_result;
  this->read_window_offset_ = 0U;
  this->first_buffer_offset_ = 0U;
  this->window_size_ = 0U;
  return {};
}

template <typename Policy>
void BufferedReader<Policy>::close()
{
  if (!file_desc_)
  {
    return;
  }
  this->maybe_reader_error_ = std::nullopt;
  file_desc_.forced_close();
  this->clear_zero_copy_array_spans();
  this->read_window_spans_.clear();
  this->read_window_buffers_.clear();
}

template <typename Policy>
[[nodiscard]] bool BufferedReader<Policy>::is_open() const noexcept
{
  return static_cast<bool>(file_desc_);
}

template <typename Policy>
[[nodiscard]] typename BufferedReader<Policy>::FilesystemType& BufferedReader<Policy>::get_filesystem()
{
  return kits_fs_;
}

template <typename Policy>
[[nodiscard]] LogExpected<std::pmr::list<std::pmr::string>>
BufferedReader<Policy>::list_log_files(std::string_view log_path)
{
  auto readdir_result = kits_fs_.read_directory(
    log_path,
    [this, log_path](const auto& dent)
    {
      const std::string_view name{&dent.d_name[0U]};
      bool is_reg = dent.d_type == DT_REG;
      if (dent.d_type == DT_UNKNOWN || dent.d_type == DT_LNK)
      {
        const auto is_reg_result = kits_fs_.is_regular_file(std::string(log_path).append("/").append(name));
        is_reg = is_reg_result && is_reg_result.value();
      }
      return is_reg && name.ends_with(log_file_suffix);
    });
  if (!readdir_result)
  {
    return jewels::unexpected(to_log_error(readdir_result.error()));
  }
  std::pmr::list<std::pmr::string> log_files{memory_resource_};
  for (auto& path : readdir_result.value())
  {
    jewels::filesystem::Path file_path{log_path, memory_resource_};
    file_path /= path;
    log_files.push_back(file_path.string());
  }
  return {std::move(log_files)};
}

template <typename Policy>
[[nodiscard]] LogExpected<void>
BufferedReader<Policy>::read_log_file_trailer(std::string_view file_name, std::span<std::byte> buffer_span)
{
  const auto open_result = kits_fs_.open(file_name);
  if (!open_result)
  {
    return jewels::unexpected(to_log_error(open_result.error()));
  }
  const auto& file_desc = open_result.value();
  const auto size_result = kits_fs_.get_size(file_desc);
  if (!size_result)
  {
    return jewels::unexpected(to_log_error(size_result.error()));
  }
  if (size_result.value() < buffer_span.size())
  {
    return jewels::unexpected(LogError::empty_log_file);
  }
  const auto read_result = kits_fs_.read(file_desc, size_result.value() - buffer_span.size(), buffer_span);
  if (!read_result)
  {
    return jewels::unexpected(to_log_error(read_result.error()));
  }
  if (read_result.value() < buffer_span.size())
  {
    return jewels::unexpected(LogError::empty_log_file);
  }
  return {};
}

template <typename Policy>
[[nodiscard]] LogExpected<void> BufferedReader<Policy>::grow_window()
{
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
  auto read_result = kits_fs_.read(file_desc_, window_end_offset, std::span{buffer_reference->data(), bytes_to_read});
  const auto end_time = jewels::time::SteadyClock::now();
  if (!read_result)
  {
    if (read_result.error() != jewels::filesystem::make_error_code(EIO))
    {
      jewels::log_cerr_error("Unrecoverable error reading log file: {}", read_result.error().message());
      this->maybe_reader_error_ = to_log_error(read_result.error());
      return jewels::unexpected{*this->maybe_reader_error_};
    }
    const auto read_with_errors_result =
      read_with_io_errors(window_end_offset, std::span{buffer_reference->data(), bytes_to_read});
    if (!read_with_errors_result)
    {
      return jewels::unexpected(read_with_errors_result.error());
    }
    read_result = *read_with_errors_result;
  }
  if (*read_result != bytes_to_read)
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

template <typename Policy>
[[nodiscard]] LogExpected<size_t> BufferedReader<Policy>::read_with_io_errors(size_t offset, std::span<std::byte> data)
{
  if (data.empty())
  {
    return 0U;
  }
  std::pmr::list<std::pair<size_t, std::span<std::byte>>> spans_to_read{memory_resource_};
  spans_to_read.emplace_front(offset, data);
  while (!spans_to_read.empty())
  {
    auto [read_offset, read_span] = spans_to_read.front();
    spans_to_read.pop_front();
    const auto read_result = kits_fs_.read(file_desc_, read_offset, read_span);
    if (read_result)
    {
      if (*read_result != read_span.size())
      {
        jewels::log_cerr_error("Encountered short read in I/O error recovery: returning end of log");
        this->maybe_reader_error_ = LogError::end_of_log;
        return jewels::unexpected{*this->maybe_reader_error_};
      }
      continue;
    }
    if (read_result.error() != jewels::filesystem::make_error_code(EIO))
    {
      this->maybe_reader_error_ = to_log_error(read_result.error());
      return jewels::unexpected{*this->maybe_reader_error_};
    }
    if (read_span.size() <= min_io_error_recover_read_size)
    {
      std::memset(read_span.data(), 0, read_span.size());
      continue;
    }
    const auto split_pos = std::min(read_span.size() / 2U, min_io_error_recover_read_size);
    spans_to_read.emplace_front(read_offset + split_pos, read_span.subspan(split_pos));
    spans_to_read.emplace_front(read_offset, read_span.subspan(0U, split_pos));
  }
  return data.size();
}

} // namespace clockwork_logging::onboard
