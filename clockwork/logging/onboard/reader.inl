// IWYU pragma: private, include "clockwork/logging/onboard/reader.hh"

#pragma once

#include "clockwork/logging/onboard/reader.hh"

#include "clockwork/logging/channel_type.hh"
#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/decompress_option.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/log_uuid.hh"
#include "clockwork/logging/message_encoding.hh"
#include "clockwork/logging/onboard/log_format.hh"
#include "clockwork/logging/onboard/types.hh"
#include "clockwork/logging/schema_encoding.hh"
#include "clockwork/logging/xxh3_checksum.hh"
#include "clockwork/logging/zstd_helper.hh"
#include "jewels/container/at.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/filesystem.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/std/span.hh"
#include "jewels/uuid/uuid.hh"

#include <fmt10/base.h>
#include <xxh3.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <dirent.h>
#include <functional>
#include <iterator>
#include <list>
#include <memory_resource>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace clockwork_logging::onboard
{

template <typename BufferedReaderType>
Reader<BufferedReaderType>::Reader(
  jewels::memory::MemoryResource memory_resource, std::string_view log_path, MetadataMapOption metadata_map_option)
  requires DiskBufferedReaderType<BufferedReaderType>
  : memory_resource_(std::move(memory_resource)),
    all_log_files_(memory_resource_),
    metadata_map_option_(metadata_map_option),
    buffered_reader_(memory_resource_),
    header_buffer_(memory_resource_),
    data_buffer_(memory_resource_),
    log_files_(memory_resource_),
    channel_id_map_(memory_resource_),
    channel_metadata_map_(memory_resource_),
    schema_id_map_(memory_resource_),
    schema_metadata_map_(memory_resource_),
    saved_persistent_message_map_(memory_resource_),
    last_persistent_message_time_map_(memory_resource_),
    decompressor_{memory_resource_}
{
  maybe_log_path_.emplace(std::pmr::string{log_path, memory_resource_});
}

template <typename BufferedReaderType>
Reader<BufferedReaderType>::Reader(
  jewels::memory::MemoryResource memory_resource,
  const std::pmr::list<jewels::filesystem::Path>& log_files,
  MetadataMapOption metadata_map_option)
  requires DiskBufferedReaderType<BufferedReaderType>
  : memory_resource_(std::move(memory_resource)),
    all_log_files_(memory_resource_),
    metadata_map_option_(metadata_map_option),
    buffered_reader_(memory_resource_),
    header_buffer_(memory_resource_),
    data_buffer_(memory_resource_),
    log_files_(memory_resource_),
    channel_id_map_(memory_resource_),
    channel_metadata_map_(memory_resource_),
    schema_id_map_(memory_resource_),
    schema_metadata_map_(memory_resource_),
    saved_persistent_message_map_(memory_resource_),
    last_persistent_message_time_map_(memory_resource_),
    all_log_file_list_initialized_(true),
    decompressor_{memory_resource_}
{
  std::ranges::for_each(
    log_files, [this](const auto& log_file) { all_log_files_.emplace_back(log_file.string_view(), memory_resource_); });
}

template <typename BufferedReaderType>
Reader<BufferedReaderType>::Reader(
  jewels::memory::MemoryResource memory_resource, std::span<const std::byte> log_data_buffer)
  requires MemoryBufferedReaderType<BufferedReaderType>
  : memory_resource_(std::move(memory_resource)),
    all_log_files_(memory_resource_),
    buffered_reader_(memory_resource_),
    header_buffer_(memory_resource_),
    data_buffer_(memory_resource_),
    log_files_(memory_resource_),
    log_data_buffer_(log_data_buffer),
    channel_id_map_(memory_resource_),
    channel_metadata_map_(memory_resource_),
    schema_id_map_(memory_resource_),
    schema_metadata_map_(memory_resource_),
    saved_persistent_message_map_(memory_resource_),
    last_persistent_message_time_map_(memory_resource_),
    all_log_file_list_initialized_(true),
    decompressor_{memory_resource_}
{
}

template <typename BufferedReaderType>
[[nodiscard]] LogExpected<std::pmr::list<jewels::filesystem::Path>>
Reader<BufferedReaderType>::list_log_files(jewels::memory::MemoryResource memory_resource, std::string_view log_path)
  requires DiskBufferedReaderType<BufferedReaderType>
{
  typename BufferedReaderType::FilesystemType filesys{memory_resource};
  auto readdir_result = filesys.read_directory(
    log_path,
    [&filesys, log_path](const auto& dent)
    {
      const std::string_view name{&dent.d_name[0U]};
      bool is_reg = dent.d_type == DT_REG;
      if (dent.d_type == DT_UNKNOWN || dent.d_type == DT_LNK)
      {
        const auto is_reg_result = filesys.is_regular_file(std::string(log_path).append("/").append(name));
        is_reg = is_reg_result && is_reg_result.value();
      }
      return is_reg && name.ends_with(log_file_suffix);
    });
  if (!readdir_result)
  {
    return jewels::unexpected(LogError::failed);
  }
  std::pmr::list<jewels::filesystem::Path> log_files{memory_resource};
  for (auto& path : readdir_result.value())
  {
    jewels::filesystem::Path file_path{log_path, memory_resource};
    file_path /= path;
    log_files.push_back(std::move(file_path));
  }
  return {std::move(log_files)};
}

template <typename BufferedReaderType>
[[nodiscard]] LogExpected<size_t> Reader<BufferedReaderType>::locate_first_log_file_for_interval(
  jewels::memory::MemoryResource memory_resource,
  LogInterval log_interval,
  const std::pmr::vector<jewels::filesystem::Path>& log_files,
  std::pmr::vector<LogExpected<LogInterval>>& interval_results)
  requires DiskBufferedReaderType<BufferedReaderType>
{
  size_t left_index = 0U;
  size_t right_index = log_files.size() - 1U;
  while (interval_results.at(left_index)->get_end_timestamp() < log_interval.get_start_timestamp())
  {
    const auto next_index = (left_index + right_index) / 2U;
    if (next_index == left_index)
    {
      left_index = right_index;
      break;
    }
    if (!interval_results.at(next_index))
    {
      interval_results.at(next_index) =
        get_file_log_interval(memory_resource, log_files.at(next_index), TimeFilterOption::log_time);
      if (!interval_results.at(next_index))
      {
        jewels::log_cerr_warn(
          "Failed to get interval for {}: {}",
          log_files.at(next_index).string_view(),
          interval_results.at(next_index).error());
        return jewels::unexpected(interval_results.at(next_index).error());
      }
    }
    if (interval_results.at(next_index)->get_end_timestamp() < log_interval.get_start_timestamp())
    {
      left_index = next_index;
    }
    else
    {
      right_index = next_index;
    }
  }
  return left_index;
}

template <typename BufferedReaderType>
[[nodiscard]] std::pmr::list<jewels::filesystem::Path> Reader<BufferedReaderType>::list_log_files_for_interval_no_fail(
  jewels::memory::MemoryResource memory_resource,
  LogInterval log_interval,
  size_t first_index,
  const std::pmr::vector<jewels::filesystem::Path>& log_files,
  std::pmr::vector<LogExpected<LogInterval>>& interval_results)
  requires DiskBufferedReaderType<BufferedReaderType>
{
  std::pmr::list<jewels::filesystem::Path> interval_files(memory_resource);
  for (size_t i = first_index; i < log_files.size(); ++i)
  {
    if (interval_results.at(i) == jewels::unexpected(LogError::not_initialized))
    {
      interval_results.at(i) = get_file_log_interval(memory_resource, log_files.at(i), TimeFilterOption::log_time);
    }
    if (interval_results.at(i))
    {
      if (interval_results.at(i)->overlaps(log_interval))
      {
        interval_files.emplace_back(log_files.at(i));
      }
      if (interval_results.at(i)->get_end_timestamp() >= log_interval.get_end_timestamp())
      {
        break;
      }
    }
  }
  return interval_files;
}

template <typename BufferedReaderType>
[[nodiscard]] LogExpected<std::pmr::list<jewels::filesystem::Path>>
Reader<BufferedReaderType>::list_log_files_for_interval(
  jewels::memory::MemoryResource memory_resource, std::string_view log_path, LogInterval log_interval)
  requires DiskBufferedReaderType<BufferedReaderType>
{
  auto list_result = list_log_files(memory_resource, log_path);
  if (!list_result || list_result->size() <= 1U)
  {
    return list_result;
  }
  std::pmr::vector<jewels::filesystem::Path> log_files(memory_resource);
  log_files.reserve(list_result->size());
  for (auto& log_file : list_result.value())
  {
    log_files.emplace_back(std::move(log_file));
  }
  std::pmr::vector<LogExpected<LogInterval>> interval_results(
    log_files.size(), jewels::unexpected(LogError::not_initialized), memory_resource);
  // Reading the last log file is expensive if the log is being written, check the second to last file first
  interval_results.at(log_files.size() - 2U) =
    get_file_log_interval(memory_resource, log_files.at(log_files.size() - 2U), TimeFilterOption::log_time);
  if (!interval_results.at(log_files.size() - 2U))
  {
    jewels::log_cerr_warn(
      "Failed to get interval for {}: {}",
      log_files.at(log_files.size() - 2U).string_view(),
      interval_results.at(log_files.size() - 2U).error());
    return list_log_files_for_interval_no_fail(memory_resource, log_interval, 0U, log_files, interval_results);
  }
  if (interval_results.at(log_files.size() - 2U)->get_end_timestamp() >= log_interval.get_end_timestamp())
  {
    log_files.pop_back();
    interval_results.pop_back();
  }
  else
  {
    interval_results.back() = get_file_log_interval(memory_resource, log_files.back(), TimeFilterOption::log_time);
    if (!interval_results.back())
    {
      // The last log file may be empty
      if (interval_results.back().error() == LogError::empty_log_file)
      {
        log_files.pop_back();
        interval_results.pop_back();
      }
      else
      {
        jewels::log_cerr_warn(
          "Failed to get interval for {}: {}", log_files.back().string_view(), interval_results.back().error());
        return list_log_files_for_interval_no_fail(memory_resource, log_interval, 0U, log_files, interval_results);
      }
    }
  }
  if (!interval_results.front())
  {
    interval_results.front() = get_file_log_interval(memory_resource, log_files.front(), TimeFilterOption::log_time);
    if (!interval_results.front())
    {
      jewels::log_cerr_warn(
        "Failed to get interval for {}: {}", log_files.front().string_view(), interval_results.front().error());
      return list_log_files_for_interval_no_fail(memory_resource, log_interval, 0U, log_files, interval_results);
    }
  }
  if (!LogInterval{interval_results.front()->get_start_timestamp(), interval_results.back()->get_end_timestamp()}
         .overlaps(log_interval))
  {
    return std::pmr::list<jewels::filesystem::Path>(memory_resource);
  }
  if (log_files.size() <= 1U)
  {
    std::pmr::list<jewels::filesystem::Path> interval_files(memory_resource);
    interval_files.emplace_back(std::move(log_files.front()));
    return interval_files;
  }
  const auto first_index_result =
    locate_first_log_file_for_interval(memory_resource, log_interval, log_files, interval_results);
  if (!first_index_result)
  {
    return list_log_files_for_interval_no_fail(memory_resource, log_interval, 0U, log_files, interval_results);
  }
  return list_log_files_for_interval_no_fail(
    memory_resource, log_interval, first_index_result.value(), log_files, interval_results);
}

template <typename BufferedReaderType>
[[nodiscard]] LogExpected<void> Reader<BufferedReaderType>::open(
  std::optional<LogInterval> maybe_log_interval,
  TimeFilterOption time_filter_option,
  DecompressOption decompress_option)
  requires DiskBufferedReaderType<BufferedReaderType>
{
  if (*this)
  {
    return jewels::unexpected(LogError::already_open);
  }
  close();
  if (const auto init_result = initialize_log_file_list(); !init_result)
  {
    return jewels::unexpected(init_result.error());
  }
  maybe_log_interval_ = maybe_log_interval;
  time_filter_option_ = time_filter_option;
  decompress_option_ = decompress_option;
  if (const auto init_result = initialize_log_metadata_maps(); !init_result)
  {
    return jewels::unexpected(init_result.error());
  }
  return {};
}

template <typename BufferedReaderType>
[[nodiscard]] LogExpected<void> Reader<BufferedReaderType>::open(
  std::optional<LogInterval> maybe_log_interval,
  TimeFilterOption time_filter_option,
  DecompressOption decompress_option)
  requires MemoryBufferedReaderType<BufferedReaderType>
{
  if (*this)
  {
    return jewels::unexpected(LogError::already_open);
  }
  close();
  if (const auto open_result = buffered_reader_.open(log_data_buffer_); !open_result)
  {
    return jewels::unexpected(open_result.error());
  }
  LogHeader log_header{};
  const auto copy_result =
    buffered_reader_.copy_out(0U, std::as_writable_bytes(jewels::as_single_item_span(log_header)));
  if (!copy_result)
  {
    ++error_counters_.invalid_log_headers;
    buffered_reader_.close();
    return jewels::unexpected(copy_result.error());
  }
  if (log_header.magic_number != log_magic_number)
  {
    ++error_counters_.invalid_log_headers;
    // Try to read the log anyway
  }
  else
  {
    const auto advance_result = buffered_reader_.advance(log_header_size);
    if (!advance_result)
    {
      ++error_counters_.advance_errors;
      buffered_reader_.close();
      return jewels::unexpected(advance_result.error());
    }
  }
  maybe_log_interval_ = maybe_log_interval;
  time_filter_option_ = time_filter_option;
  decompress_option_ = decompress_option;
  return {};
}

template <typename BufferedReaderType>
void Reader<BufferedReaderType>::close()
{
  buffered_reader_.close();
  maybe_current_message_record_size_ = std::nullopt;
  error_counters_ = {};
  maybe_current_message_record_size_ = std::nullopt;
  header_buffer_.clear();
  data_buffer_.clear();
  channel_id_map_.clear();
  channel_metadata_map_.clear();
  schema_id_map_.clear();
  schema_metadata_map_.clear();
  log_files_.clear();
  log_metadata_maps_initialized_ = false;
  log_file_list_initialized_ = false;
  first_message_read_ = false;
}

template <typename BufferedReaderType>
[[nodiscard]] Reader<BufferedReaderType>::operator bool() const noexcept
{
  return !log_files_.empty() || buffered_reader_;
}

template <typename BufferedReaderType>
[[nodiscard]] LogExpected<LoggedMessage> Reader<BufferedReaderType>::read_next()
{
  const auto read_result = zero_copy_read_next();
  if (!read_result)
  {
    return jewels::unexpected(read_result.error());
  }
  const auto& zero_copy_message = *read_result;
  std::span<const std::byte> data_span{};
  if (zero_copy_message.data.size() == 1U)
  {
    data_span = zero_copy_message.data.front();
  }
  else if (zero_copy_message.data.size() > 1U)
  {
    data_buffer_.resize(data_spans_size(zero_copy_message.data));
    copy_data_spans(zero_copy_message.data, std::span{data_buffer_});
    data_span = std::span{data_buffer_};
  }
  return LoggedMessage{
    .channel_name = zero_copy_message.channel_name,
    .sequence_number = zero_copy_message.sequence_number,
    .log_time = zero_copy_message.log_time,
    .message_time = zero_copy_message.message_time,
    .header = zero_copy_message.header,
    .data = data_span,
    .message_type = zero_copy_message.message_type,
    .message_encoding = zero_copy_message.message_encoding,
    .is_lite_compressed = zero_copy_message.is_lite_compressed,
  };
}

template <typename BufferedReaderType>
[[nodiscard]] LogExpected<ZeroCopyLoggedMessage> Reader<BufferedReaderType>::zero_copy_read_next()
{
  if (!saved_persistent_message_map_.empty())
  {
    saved_persistent_message_map_.erase(saved_persistent_message_map_.begin());
  }
  if (!saved_persistent_message_map_.empty() && maybe_first_logged_message_)
  {
    const auto& [channel_name, saved_message] = *saved_persistent_message_map_.begin();
    first_message_read_ = true;
    return to_zero_copy_logged_message(channel_name, saved_message, maybe_first_logged_message_->message_time);
  }
  if (maybe_first_logged_message_)
  {
    std::optional<ZeroCopyLoggedMessage> maybe_message{std::nullopt};
    maybe_first_logged_message_.swap(maybe_message);
    first_message_read_ = true;
    return maybe_message.value();
  }
  if (maybe_current_message_record_size_)
  {
    advance_past_current_record(*maybe_current_message_record_size_);
    maybe_current_message_record_size_ = std::nullopt;
  }
  auto scan_result = scan_for_next_log_message();
  if (scan_result)
  {
    first_message_read_ = true;
  }
  return scan_result;
}

template <typename BufferedReaderType>
[[nodiscard]] LogExpected<ZeroCopyLoggedMessage> Reader<BufferedReaderType>::scan_for_next_log_message()
{
  while (buffered_reader_ || !log_files_.empty())
  {
    if constexpr (IsDiskBufferedReader<BufferedReaderType>::value)
    {
      if (!buffered_reader_)
      {
        const auto file_name = std::move(log_files_.front());
        log_files_.pop_front();
        if (const auto open_result = open_log_file(file_name); !open_result)
        {
          return jewels::unexpected(open_result.error());
        }
        if (!buffered_reader_)
        {
          continue;
        }
      }
    }
    const auto read_header_result = read_next_record_header();
    if (!read_header_result)
    {
      // read_next_record_header updated the error counters
      continue;
    }
    const auto& record_header = read_header_result.value();
    switch (record_header.record_type)
    {
    case RecordType::schema:
      process_schema_record(record_header);
      break;
    case RecordType::channel:
      process_channel_record(record_header);
      break;
    case RecordType::message:
    {
      if (const auto message_result = try_process_next_message(record_header); message_result)
      {
        return *message_result;
      }
      break;
    }
    case RecordType::message_v2:
      jewels::log_cerr_error("MESSAGE HEADER V2 IS NOT SUPPORTED");
      return jewels::unexpected(LogError::not_implemented);
      break;
    case RecordType::end_log_file:
      process_end_log_file_record(record_header);
      break;
    default:
      scan_for_next_record();
      break;
    }
    if (maybe_current_message_record_size_)
    {
      advance_past_current_record(*maybe_current_message_record_size_);
      maybe_current_message_record_size_ = std::nullopt;
    }
  }
  return jewels::unexpected(LogError::end_of_log);
}

template <typename BufferedReaderType>
[[nodiscard]] LogExpected<LoggedChannelMetadata>
Reader<BufferedReaderType>::get_channel_metadata(std::string_view channel_name)
{
  const auto channel_metadata_map_iter = channel_metadata_map_.find(channel_name);
  if (channel_metadata_map_iter == channel_metadata_map_.end())
  {
    return jewels::unexpected(LogError::unknown_channel);
  }
  const auto& channel_metadata = channel_metadata_map_iter->second;
  LoggedChannelMetadata metadata{};
  metadata.channel_name = channel_metadata.channel_name;
  metadata.compression_type = channel_metadata.compression_type;
  metadata.message_encoding = channel_metadata.message_encoding;
  metadata.channel_type = channel_metadata.channel_type;
  if (channel_metadata.schema_id != 0U)
  {
    auto schema_id_map_iter = schema_id_map_.find(channel_metadata.schema_id);
    if (schema_id_map_iter == schema_id_map_.end())
    {
      handle_missing_schema_metadata(channel_metadata.schema_id);
      schema_id_map_iter = schema_id_map_.find(channel_metadata.schema_id);
    }
    metadata.schema_name = schema_id_map_iter->second;
    const auto schema_metadata_map_iter = schema_metadata_map_.find(metadata.schema_name);
    if (schema_metadata_map_iter == schema_metadata_map_.end())
    {
      return jewels::unexpected(LogError::failed);
    }
    const auto& schema_metadata = schema_metadata_map_iter->second;
    metadata.schema_encoding = schema_metadata.schema_encoding;
    metadata.schema_definition = schema_metadata.schema_definition;
  }
  return metadata;
}

template <typename BufferedReaderType>
[[nodiscard]] LogExpected<std::pmr::unordered_map<std::string_view, LoggedChannelMetadata>>
Reader<BufferedReaderType>::get_channel_metadata_map()
  requires DiskBufferedReaderType<BufferedReaderType>
{
  if (metadata_map_option_ == MetadataMapOption::disable)
  {
    return jewels::unexpected(LogError::metadata_map_disabled);
  }
  if (const auto init_result = initialize_log_metadata_maps(); !init_result)
  {
    return jewels::unexpected(init_result.error());
  }
  std::pmr::unordered_map<std::string_view, LoggedChannelMetadata> channel_metadata_map{memory_resource_};
  for (const auto channel_name : std::views::keys(channel_metadata_map_))
  {
    const auto metadata_result = get_channel_metadata(channel_name);
    if (!metadata_result)
    {
      return jewels::unexpected(metadata_result.error());
    }
    channel_metadata_map.emplace(channel_name, metadata_result.value());
  }
  return {std::move(channel_metadata_map)};
}

template <typename BufferedReaderType>
[[nodiscard]] const ReaderErrorCounters& Reader<BufferedReaderType>::get_error_counters() const noexcept
{
  return error_counters_;
}

template <typename BufferedReaderType>
[[nodiscard]] BufferedReaderType& Reader<BufferedReaderType>::buffered_reader()
{
  return buffered_reader_;
}

template <typename BufferedReaderType>
[[nodiscard]] const BufferedReaderType& Reader<BufferedReaderType>::buffered_reader() const
{
  return buffered_reader_;
}

template <typename BufferedReaderType>
[[nodiscard]] LogExpected<LogInterval> Reader<BufferedReaderType>::get_file_log_interval(
  jewels::memory::MemoryResource memory_resource, std::string_view file_name, TimeFilterOption time_filter_option)
  requires DiskBufferedReaderType<BufferedReaderType>
{
  const jewels::filesystem::Path file_path{file_name, memory_resource};
  jewels::filesystem::Filesystem kits_fs{memory_resource};
  kits_fs.set_verbosity(jewels::filesystem::Filesystem::ErrorVerbosity::verbose);
  const auto open_result = kits_fs.open(file_name);
  if (!open_result)
  {
    return jewels::unexpected(to_log_error(open_result.error()));
  }
  const auto& file_desc = open_result.value();
  const auto size_result = kits_fs.get_size(file_desc);
  if (!size_result)
  {
    return jewels::unexpected(to_log_error(size_result.error()));
  }
  std::array<std::byte, end_log_file_record_header_size + record_trailer_size> data_buffer{};
  if (size_result.value() >= data_buffer.size())
  {
    if (const auto read_result = kits_fs.read(file_desc, size_result.value() - data_buffer.size(), data_buffer);
        !read_result)
    {
      return jewels::unexpected(to_log_error(read_result.error()));
    }
    EndLogFileRecordHeader header{};
    std::memcpy(&header, data_buffer.data(), end_log_file_record_header_size);
    RecordTrailer trailer{};
    std::memcpy(&trailer, &data_buffer.at(end_log_file_record_header_size), record_trailer_size);
    if (try_validate_end_log_record(header, trailer))
    {
      if (!header.has_messages)
      {
        return jewels::unexpected(LogError::empty_log_file);
      }
      switch (time_filter_option)
      {
      case TimeFilterOption::log_time:
        return LogInterval{LogTimestamp{header.min_log_time_ns}, LogTimestamp{header.max_log_time_ns}};
      case TimeFilterOption::message_time:
        return LogInterval{LogTimestamp{header.min_message_time_ns}, LogTimestamp{header.max_message_time_ns}};
      }
    }
  }
  jewels::log_cerr_warn("Failed to find an end log record, reading {}", file_name);
  return get_file_log_interval_from_log(memory_resource, file_name, time_filter_option);
}

template <typename BufferedReaderType>
[[nodiscard]] LogExpected<LogInterval> Reader<BufferedReaderType>::get_file_log_interval_from_log(
  jewels::memory::MemoryResource memory_resource, std::string_view file_name, TimeFilterOption time_filter_option)
  requires DiskBufferedReaderType<BufferedReaderType>
{
  Reader reader{memory_resource, file_name, MetadataMapOption::disable};
  if (const auto open_result = reader.open_log_file(file_name); !open_result)
  {
    return jewels::unexpected(open_result.error());
  }
  std::optional<LogInterval> maybe_log_interval;
  while (reader.buffered_reader_)
  {
    const auto read_header_result = reader.read_next_record_header();
    if (!read_header_result)
    {
      break;
    }
    const auto& record_header = read_header_result.value();
    jewels::expected<ZeroCopyLoggedMessage, jewels::MonoError> message_result = jewels::unexpected(jewels::MonoError{});
    switch (record_header.record_type)
    {
    case RecordType::message:
      message_result = reader.try_process_message_record(record_header);
      break;
    case RecordType::message_v2:
      jewels::log_cerr_error("MESSAGE HEADER V2 IS NOT SUPPORTED");
      return jewels::unexpected(LogError::not_implemented);
      break;
    default:
      reader.scan_for_next_record();
      break;
    }
    if (message_result && message_result->message_type != LoggedMessageType::repeated_persistent)
    {
      if (!maybe_log_interval)
      {
        switch (time_filter_option)
        {
        case TimeFilterOption::log_time:
          maybe_log_interval = LogInterval{message_result->log_time, message_result->log_time};
          break;
        case TimeFilterOption::message_time:
          maybe_log_interval = LogInterval{message_result->message_time, message_result->message_time};
          break;
        }
      }
      else
      {
        switch (time_filter_option)
        {
        case TimeFilterOption::log_time:
          maybe_log_interval->add_timestamp(message_result->log_time);
          break;
        case TimeFilterOption::message_time:
          maybe_log_interval->add_timestamp(message_result->message_time);
          break;
        }
      }
    }
    if (reader.maybe_current_message_record_size_)
    {
      reader.advance_past_current_record(*reader.maybe_current_message_record_size_);
      reader.maybe_current_message_record_size_ = std::nullopt;
    }
  }
  reader.buffered_reader_.close();
  if (!maybe_log_interval)
  {
    jewels::log_cerr_warn("Failed to get log interval from empty log file '{}'", file_name);
    return jewels::unexpected(LogError::empty_log_file);
  }
  return *maybe_log_interval;
}

template <typename BufferedReaderType>
[[nodiscard]] LogExpected<LogInterval> Reader<BufferedReaderType>::get_memory_log_interval(
  std::span<const std::byte> log_buffer, TimeFilterOption time_filter_option)
  requires MemoryBufferedReaderType<BufferedReaderType>
{
  if (log_buffer.size() < end_log_file_record_header_size + record_trailer_size)
  {
    jewels::log_cerr_error("Log buffer too small for end log file record");
    return jewels::unexpected(LogError::read_error);
  }
  EndLogFileRecordHeader header{};
  const auto record_offset = log_buffer.size() - end_log_file_record_header_size - record_trailer_size;
  std::memcpy(&header, &log_buffer[record_offset], end_log_file_record_header_size);
  RecordTrailer trailer{};
  std::memcpy(&trailer, &log_buffer[record_offset + end_log_file_record_header_size], record_trailer_size);
  if (!try_validate_end_log_record(header, trailer))
  {
    jewels::log_cerr_error("Failed to validate the end log file record");
    return jewels::unexpected(LogError::read_error);
  }
  if (!header.has_messages)
  {
    jewels::log_cerr_error("Failed to get time range from an empty log");
    return jewels::unexpected(LogError::empty_log_file);
  }
  switch (time_filter_option)
  {
  case TimeFilterOption::log_time:
    return LogInterval{LogTimestamp{header.min_log_time_ns}, LogTimestamp{header.max_log_time_ns}};
  case TimeFilterOption::message_time:
    return LogInterval{LogTimestamp{header.min_message_time_ns}, LogTimestamp{header.max_message_time_ns}};
  }
  jewels::log_cerr_error("Failed to find an end log record");
  return jewels::unexpected(LogError::read_error);
}

template <typename BufferedReaderType>
[[nodiscard]] LogExpected<void> Reader<BufferedReaderType>::initialize_all_log_file_list()
  requires DiskBufferedReaderType<BufferedReaderType>
{
  if (all_log_file_list_initialized_)
  {
    return {};
  }
  if (!maybe_log_path_)
  {
    return jewels::unexpected(LogError::invalid_log_uri);
  }
  auto list_result = list_log_files(memory_resource_, *maybe_log_path_);
  if (!list_result)
  {
    return jewels::unexpected(list_result.error());
  }
  all_log_files_ = std::move(list_result).value();
  all_log_file_list_initialized_ = true;
  return {};
}

template <typename BufferedReaderType>
[[nodiscard]] LogExpected<void> Reader<BufferedReaderType>::initialize_log_file_list()
  requires DiskBufferedReaderType<BufferedReaderType>
{
  if (log_file_list_initialized_)
  {
    return {};
  }
  if (const auto init_result = initialize_all_log_file_list(); !init_result)
  {
    return jewels::unexpected(init_result.error());
  }
  std::ranges::for_each(
    all_log_files_,
    [this](const auto& log_file) { log_files_.emplace_back(log_file.string_view(), memory_resource_); });
  log_file_list_initialized_ = true;
  return {};
}

template <typename BufferedReaderType>
[[nodiscard]] LogExpected<void> Reader<BufferedReaderType>::initialize_log_metadata_maps()
  requires DiskBufferedReaderType<BufferedReaderType>
{
  if (log_metadata_maps_initialized_)
  {
    return {};
  }
  if (const auto init_result = initialize_all_log_file_list(); !init_result)
  {
    return jewels::unexpected(init_result.error());
  }
  if (const auto read_result = read_metadata_records(); !read_result)
  {
    return jewels::unexpected(read_result.error());
  }
  log_metadata_maps_initialized_ = true;
  return {};
}

template <typename BufferedReaderType>
[[nodiscard]] LogExpected<void> Reader<BufferedReaderType>::open_log_file(std::string_view file_name)
  requires DiskBufferedReaderType<BufferedReaderType>
{
  buffered_reader_.close();
  const auto open_result = buffered_reader_.open(file_name);
  if (!open_result)
  {
    ++error_counters_.open_failures;
    return jewels::unexpected(open_result.error());
  }
  LogHeader log_header{};
  const auto copy_result =
    buffered_reader_.copy_out(0U, std::as_writable_bytes(jewels::as_single_item_span(log_header)));
  if (!copy_result)
  {
    ++error_counters_.invalid_log_headers;
    buffered_reader_.close();
    return {};
  }
  if (log_header.magic_number != log_magic_number)
  {
    ++error_counters_.invalid_log_headers;
    // Try to read the log anyway
  }
  else
  {
    const auto advance_result = buffered_reader_.advance(log_header_size);
    if (!advance_result)
    {
      ++error_counters_.advance_errors;
      buffered_reader_.close();
    }
  }
  return {};
}

template <typename BufferedReaderType>
void Reader<BufferedReaderType>::scan_for_next_record()
{
  auto advance_result = buffered_reader_.advance(1U);
  if (!advance_result)
  {
    ++error_counters_.advance_errors;
    buffered_reader_.close();
    return;
  }
  advance_result = buffered_reader_.advance(std::as_bytes(std::span{record_magic_number}));
  if (!advance_result)
  {
    ++error_counters_.advance_errors;
    buffered_reader_.close();
  }
}

template <typename BufferedReaderType>
void Reader<BufferedReaderType>::advance_past_current_record(size_t record_size)
{
  auto advance_result = buffered_reader_.advance(record_size);
  if (!advance_result)
  {
    ++error_counters_.advance_errors;
    buffered_reader_.close();
    return;
  }
}

template <typename BufferedReaderType>
[[nodiscard]] LogExpected<RecordHeader> Reader<BufferedReaderType>::read_next_record_header()
{
  while (buffered_reader_)
  {
    const auto skip_result = buffered_reader_.skip_pad_bytes();
    if (!skip_result)
    {
      ++error_counters_.advance_errors;
      buffered_reader_.close();
      return jewels::unexpected(LogError::end_of_log);
    }
    if (!buffered_reader_)
    {
      // We advanced to end of file
      buffered_reader_.close();
      return jewels::unexpected(LogError::end_of_log);
    }
    RecordHeader record_header{};
    const auto copy_result =
      buffered_reader_.copy_out(0U, std::as_writable_bytes(jewels::as_single_item_span(record_header)));
    if (!copy_result)
    {
      ++error_counters_.invalid_records;
      buffered_reader_.close();
      return jewels::unexpected(LogError::end_of_log);
    }
    if (record_header.magic_number != record_magic_number || record_header.record_size > max_log_record_size)
    {
      ++error_counters_.invalid_records;
      scan_for_next_record();
      continue;
    }
    return record_header;
  }
  return jewels::unexpected(LogError::end_of_log);
}

template <typename BufferedReaderType>
void Reader<BufferedReaderType>::process_schema_record(const RecordHeader& record_header)
{
  SchemaRecordHeader header{};
  header.header = record_header;
  const auto copy_result = buffered_reader_.copy_out(
    record_header_size,
    std::as_writable_bytes(jewels::as_single_item_span(header)).last(schema_record_header_size - record_header_size));
  if (!copy_result)
  {
    ++error_counters_.invalid_records;
    buffered_reader_.close();
    return;
  }
  if (schema_record_header_size + header.schema_name_length + record_trailer_size > header.header.record_size)
  {
    ++error_counters_.invalid_records;
    scan_for_next_record();
    return;
  }
  const auto schema_definition_size =
    header.header.record_size - schema_record_header_size - header.schema_name_length - record_trailer_size;
  const auto zero_copy_result = buffered_reader_.zero_copy_out(
    schema_record_header_size, header.schema_name_length, schema_definition_size, record_trailer_size);
  if (!zero_copy_result)
  {
    ++error_counters_.invalid_records;
    scan_for_next_record();
    return;
  }
  auto xxh3_state = init_xxh3_checksum();
  update_xxh3_checksum(xxh3_state, std::as_bytes(jewels::as_single_item_span(header)));
  update_xxh3_checksum(xxh3_state, jewels::at(*zero_copy_result, 0));
  update_xxh3_checksum(xxh3_state, jewels::at(*zero_copy_result, 1));
  const auto xxh3_checksum = digest_xxh3_checksum(xxh3_state);
  RecordTrailer trailer{};
  copy_data_spans(jewels::at(*zero_copy_result, 2), std::as_writable_bytes(jewels::as_single_item_span(trailer)));
  if (xxh3_checksum != trailer.xxh3_checksum)
  {
    ++error_counters_.invalid_records;
    scan_for_next_record();
    return;
  }
  if (schema_id_map_.contains(header.schema_id))
  {
    advance_past_current_record(header.header.record_size);
    return;
  }
  std::pmr::string schema_name{memory_resource_};
  schema_name.resize(header.schema_name_length);
  copy_data_spans(jewels::at(*zero_copy_result, 0), std::as_writable_bytes(std::span{schema_name}));
  std::pmr::string schema_definition{memory_resource_};
  schema_definition.resize(schema_definition_size);
  copy_data_spans(jewels::at(*zero_copy_result, 1), std::as_writable_bytes(std::span{schema_definition}));
  auto schema_encoding = header.schema_encoding;
  if (schema_encoding == SchemaEncoding::clockwork_tachyon_zstd)
  {
    const auto decompress_result =
      zstd_decompress(std::as_bytes(std::span{schema_definition.data(), schema_definition.size()}), memory_resource_);
    if (!decompress_result)
    {
      ++error_counters_.invalid_records;
      scan_for_next_record();
      return;
    }
    schema_definition.resize(decompress_result->size());
    std::memcpy(schema_definition.data(), decompress_result->data(), decompress_result->size());
    schema_encoding = SchemaEncoding::clockwork_tachyon;
  }
  const auto map_iter = schema_id_map_.emplace(header.schema_id, std::move(schema_name)).first;
  schema_metadata_map_.emplace(
    map_iter->second,
    SchemaMetadata{
      .schema_name = map_iter->second,
      .schema_encoding = schema_encoding,
      .schema_definition = std::move(schema_definition),
    });
  advance_past_current_record(header.header.record_size);
}

template <typename BufferedReaderType>
void Reader<BufferedReaderType>::process_channel_record(const RecordHeader& record_header)
{
  ChannelRecordHeader header{};
  header.header = record_header;
  const auto copy_result = buffered_reader_.copy_out(
    record_header_size,
    std::as_writable_bytes(jewels::as_single_item_span(header)).last(channel_record_header_size - record_header_size));
  if (!copy_result)
  {
    ++error_counters_.invalid_records;
    buffered_reader_.close();
    return;
  }
  if (channel_record_header_size + record_trailer_size > header.header.record_size)
  {
    ++error_counters_.invalid_records;
    scan_for_next_record();
    return;
  }
  const auto channel_name_length = header.header.record_size - channel_record_header_size - record_trailer_size;
  const auto zero_copy_result =
    buffered_reader_.zero_copy_out(channel_record_header_size, channel_name_length, record_trailer_size, 0U);
  if (!zero_copy_result)
  {
    ++error_counters_.invalid_records;
    scan_for_next_record();
    return;
  }
  auto xxh3_state = init_xxh3_checksum();
  update_xxh3_checksum(xxh3_state, std::as_bytes(jewels::as_single_item_span(header)));
  update_xxh3_checksum(xxh3_state, jewels::at(*zero_copy_result, 0));
  const auto xxh3_checksum = digest_xxh3_checksum(xxh3_state);
  RecordTrailer trailer{};
  copy_data_spans(jewels::at(*zero_copy_result, 1), std::as_writable_bytes(jewels::as_single_item_span(trailer)));
  if (xxh3_checksum != trailer.xxh3_checksum)
  {
    ++error_counters_.invalid_records;
    scan_for_next_record();
    return;
  }
  if (channel_id_map_.contains(header.channel_id))
  {
    advance_past_current_record(header.header.record_size);
    return;
  }
  std::pmr::string channel_name{memory_resource_};
  channel_name.resize(channel_name_length);
  copy_data_spans(jewels::at(*zero_copy_result, 0), std::as_writable_bytes(std::span{channel_name}));
  const auto map_iter = channel_id_map_
                          .emplace(
                            header.channel_id,
                            ChannelIdMapEntry{
                              .channel_name = std::move(channel_name),
                              .message_encoding = header.message_encoding,
                            })
                          .first;
  channel_metadata_map_.emplace(
    map_iter->second.channel_name,
    ChannelMetadata{
      .channel_name = map_iter->second.channel_name,
      .compression_type = header.compression_type,
      .message_encoding = map_iter->second.message_encoding,
      .schema_id = header.schema_id,
      .channel_type = header.flags.is_persistent ? ChannelType::persistent : ChannelType::regular,
    });
  advance_past_current_record(header.header.record_size);
}

template <typename BufferedReaderType>
[[nodiscard]] jewels::expected<ZeroCopyLoggedMessage, jewels::MonoError>
Reader<BufferedReaderType>::try_process_next_message(const RecordHeader& record_header)
{
  auto message_result = try_process_message_record(record_header);
  if (!message_result)
  {
    return jewels::unexpected(jewels::MonoError{});
  }
  auto& message = *message_result;
  if (
    ((message.message_type == LoggedMessageType::repeated_persistent) &&
     (!last_persistent_message_time_map_.contains(message.channel_name) ||
      (message.message_time != last_persistent_message_time_map_.at(message.channel_name)))) ||
    ((message.message_type == LoggedMessageType::persistent) && maybe_log_interval_ &&
     (((time_filter_option_ == TimeFilterOption::log_time) &&
       (message.log_time < maybe_log_interval_->get_start_timestamp())) ||
      ((time_filter_option_ == TimeFilterOption::message_time) &&
       (message.message_time < maybe_log_interval_->get_start_timestamp())))))
  {
    if (
      first_message_read_ && ((message.message_type == LoggedMessageType::repeated_persistent) &&
                              (!last_persistent_message_time_map_.contains(message.channel_name) ||
                               (message.message_time != last_persistent_message_time_map_.at(message.channel_name)))))
    {
      // After we start reading a new repeated persistent message means that we dropped the original.
      // Change the type of this one to persistent to ensure it gets into the offloaded logs.
      message.message_type = LoggedMessageType::persistent;
    }
    else
    {
      message.message_type = LoggedMessageType::repeated_persistent;
    }
    last_persistent_message_time_map_.insert_or_assign(message.channel_name, message.message_time);
    auto& saved_msg = saved_persistent_message_map_
                        .insert_or_assign(
                          message.channel_name,
                          SavedPersistentMessage{
                            .sequence_number = message.sequence_number,
                            .log_time = message.log_time,
                            .message_time = message.message_time,
                            .header = {},
                            .data = {},
                            .data_span = {},
                            .message_type = message.message_type,
                            .message_encoding = message.message_encoding,
                            .is_lite_compressed = message.is_lite_compressed,
                          })
                        .first->second;
    saved_msg.header = std::pmr::vector<std::byte>(message.header.size(), memory_resource_);
    if (!message.header.empty())
    {
      std::memcpy(saved_msg.header.data(), message.header.data(), message.header.size());
    }
    saved_msg.data = std::pmr::vector<std::byte>(data_spans_size(message.data), memory_resource_);
    copy_data_spans(message.data, std::span{saved_msg.data});
    saved_msg.data_span = std::span{saved_msg.data};
    return jewels::unexpected(jewels::MonoError{});
  }
  if (message.message_type == LoggedMessageType::repeated_persistent)
  {
    return jewels::unexpected(jewels::MonoError{});
  }
  if (
    maybe_log_interval_ &&
    !maybe_log_interval_->contains(
      time_filter_option_ == TimeFilterOption::log_time ? message.log_time : message.message_time))
  {
    return jewels::unexpected(jewels::MonoError{});
  }
  if (message.message_type == LoggedMessageType::persistent)
  {
    last_persistent_message_time_map_.insert_or_assign(message.channel_name, message.message_time);
    saved_persistent_message_map_.erase(message.channel_name);
  }
  if (!saved_persistent_message_map_.empty())
  {
    maybe_first_logged_message_ = message;
    const auto& [channel_name, saved_message] = *saved_persistent_message_map_.begin();
    return to_zero_copy_logged_message(
      channel_name, saved_message, first_message_read_ ? saved_message.message_time : message.message_time);
  }
  return message_result;
}

template <typename BufferedReaderType>
[[nodiscard]] jewels::expected<ZeroCopyLoggedMessage, jewels::MonoError>
Reader<BufferedReaderType>::try_process_message_record(const RecordHeader& record_header)
{
  MessageRecordHeader header{};
  header.header = record_header;
  const auto copy_result = buffered_reader_.copy_out(
    record_header_size,
    std::as_writable_bytes(jewels::as_single_item_span(header)).last(message_record_header_size - record_header_size));
  if (!copy_result)
  {
    ++error_counters_.invalid_records;
    buffered_reader_.close();
    jewels::log_cerr_error("copy_result: {}", copy_result.error());
    return jewels::unexpected(jewels::MonoError{});
  }
  if (message_record_header_size + header.header_length + record_trailer_size > header.header.record_size)
  {
    ++error_counters_.invalid_records;
    scan_for_next_record();
    jewels::log_cerr_error("invalid message size");
    return jewels::unexpected(jewels::MonoError{});
  }
  const auto data_size =
    header.header.record_size - message_record_header_size - header.header_length - record_trailer_size;
  const auto zero_copy_result =
    buffered_reader_.zero_copy_out(message_record_header_size, header.header_length, data_size, record_trailer_size);
  if (!zero_copy_result)
  {
    ++error_counters_.invalid_records;
    scan_for_next_record();
    jewels::log_cerr_error("zero_copy_result: {}", zero_copy_result.error());
    return jewels::unexpected(jewels::MonoError{});
  }
  auto xxh3_state = init_xxh3_checksum();
  update_xxh3_checksum(xxh3_state, std::as_bytes(jewels::as_single_item_span(header)));
  update_xxh3_checksum(xxh3_state, jewels::at(*zero_copy_result, 0));
  update_xxh3_checksum(xxh3_state, jewels::at(*zero_copy_result, 1));
  const auto xxh3_checksum = digest_xxh3_checksum(xxh3_state);
  RecordTrailer trailer{};

  copy_data_spans(jewels::at(*zero_copy_result, 2), std::as_writable_bytes(jewels::as_single_item_span(trailer)));
  if (xxh3_checksum != trailer.xxh3_checksum)
  {
    ++error_counters_.invalid_records;
    scan_for_next_record();
    return jewels::unexpected(jewels::MonoError{});
  }
  auto channel_id_map_iter = channel_id_map_.find(header.channel_id);
  if (channel_id_map_iter == channel_id_map_.end())
  {
    handle_missing_channel_metadata(header.channel_id);
    channel_id_map_iter = channel_id_map_.find(header.channel_id);
  }
  maybe_current_message_record_size_ = header.header.record_size;
  auto message_data = jewels::at(*zero_copy_result, 1);
  bool is_lite_compressed = false;
  if (header.flags.is_lite_compressed != 0U)
  {
    if (decompress_option_ == DecompressOption::decompress)
    {
      const auto decompress_result = decompressor_.zero_copy_decompress(message_data);
      if (!decompress_result)
      {
        ++error_counters_.invalid_records;
        scan_for_next_record();
        return jewels::unexpected(jewels::MonoError{});
      }
      message_data = decompress_result.value();
    }
    else
    {
      is_lite_compressed = true;
    }
  }

  LoggedMessageType message_type = LoggedMessageType::regular;
  if (header.flags.is_repeated && header.flags.is_persistent)
  {
    message_type = LoggedMessageType::repeated_persistent;
  }
  else if (header.flags.is_persistent)
  {
    message_type = LoggedMessageType::persistent;
  }

  const auto header_spans = jewels::at(*zero_copy_result, 0);
  std::span<const std::byte> header_span{};
  if (header_spans.size() == 1U)
  {
    header_span = header_spans.front();
  }
  else if (header_spans.size() > 1U)
  {
    header_buffer_.resize(data_spans_size(header_spans));
    copy_data_spans(header_spans, std::span{header_buffer_});
    header_span = std::span{header_buffer_};
  }

  return ZeroCopyLoggedMessage{
    .channel_name = channel_id_map_iter->second.channel_name,
    .sequence_number = header.sequence_number,
    .log_time = LogTimestamp{header.log_time_ns},
    .message_time = LogTimestamp{header.message_time_ns},
    .header = header_span,
    .data = message_data,
    .message_type = message_type,
    .message_encoding = channel_id_map_iter->second.message_encoding,
    .is_lite_compressed = is_lite_compressed,
  };
}

template <typename BufferedReaderType>
void Reader<BufferedReaderType>::process_end_log_file_record(const RecordHeader& record_header)
{
  EndLogFileRecordHeader header{};
  header.header = record_header;
  if (const auto copy_result = buffered_reader_.copy_out(
        record_header_size,
        std::as_writable_bytes(jewels::as_single_item_span(header))
          .last(end_log_file_record_header_size - record_header_size));
      !copy_result)
  {
    ++error_counters_.invalid_records;
    buffered_reader_.close();
    return;
  }
  RecordTrailer trailer{};
  if (const auto copy_result = buffered_reader_.copy_out(
        end_log_file_record_header_size, std::as_writable_bytes(jewels::as_single_item_span(trailer)));
      !copy_result)
  {
    ++error_counters_.invalid_records;
    buffered_reader_.close();
    return;
  }
  if (const auto process_result = try_validate_end_log_record(header, trailer); !process_result)
  {
    ++error_counters_.invalid_records;
    scan_for_next_record();
    return;
  }
  advance_past_current_record(header.header.record_size);
}

template <typename BufferedReaderType>
[[nodiscard]] jewels::expected<void, jewels::MonoError> Reader<BufferedReaderType>::try_validate_end_log_record(
  const EndLogFileRecordHeader& record_header, const RecordTrailer& trailer)
{
  const auto xxh3_checksum = compute_xxh3_checksum(std::as_bytes(jewels::as_single_item_span(record_header)));
  if (xxh3_checksum != trailer.xxh3_checksum)
  {
    return jewels::unexpected(jewels::MonoError{});
  }
  return {};
}

template <typename BufferedReaderType>
[[nodiscard]] LogExpected<void> Reader<BufferedReaderType>::read_metadata_records()
{
  for (auto log_file_iter = all_log_files_.begin(); log_file_iter != all_log_files_.end(); ++log_file_iter)
  {
    const auto& file_name = *log_file_iter;
    if (const auto open_result = open_log_file(file_name); !open_result)
    {
      return jewels::unexpected(open_result.error());
    }
    const bool is_last_file = std::next(log_file_iter) == all_log_files_.end();
    bool reached_messages = false;
    while (buffered_reader_ &&
           (!reached_messages || (metadata_map_option_ == MetadataMapOption::enable && is_last_file)))
    {
      const auto read_header_result = read_next_record_header();
      if (!read_header_result)
      {
        // read_next_record_header updated the error counters
        break;
      }
      const auto& record_header = read_header_result.value();
      switch (record_header.record_type)
      {
      case RecordType::schema:
        process_schema_record(record_header);
        break;
      case RecordType::channel:
        process_channel_record(record_header);
        break;
      case RecordType::message:
        reached_messages = true;
        if (metadata_map_option_ == MetadataMapOption::enable && is_last_file)
        {
          scan_for_next_record();
        }
        break;
      case RecordType::message_v2:
        jewels::log_cerr_error("MESSAGE HEADER V2 IS NOT SUPPORTED");
        return jewels::unexpected(LogError::not_implemented);
      default:
        scan_for_next_record();
        break;
      }
    }
    buffered_reader_.close();
  }
  return {};
}

template <typename BufferedReaderType>
void Reader<BufferedReaderType>::handle_missing_schema_metadata(uint16_t schema_id)
{
  ++error_counters_.missing_schema_metadata;
  const auto schema_uuid = LogUuid::random_uuid();
  std::pmr::string schema_name{memory_resource_};
  fmt::format_to(std::back_inserter(schema_name), "missing_schema_{}", schema_uuid.to_string(memory_resource_));
  const auto map_iter = schema_id_map_.emplace(schema_id, std::move(schema_name)).first;
  schema_metadata_map_.emplace(
    map_iter->second,
    SchemaMetadata{
      .schema_name = map_iter->second,
      .schema_encoding = SchemaEncoding::undefined,
      .schema_definition = {},
    });
}

template <typename BufferedReaderType>
void Reader<BufferedReaderType>::handle_missing_channel_metadata(uint16_t channel_id)
{
  ++error_counters_.missing_channel_metadata;
  const auto channel_uuid = LogUuid::random_uuid();
  std::pmr::string channel_name{memory_resource_};
  fmt::format_to(std::back_inserter(channel_name), "missing_channel_{}", channel_uuid.to_string(memory_resource_));
  const auto map_iter = channel_id_map_
                          .emplace(
                            channel_id,
                            ChannelIdMapEntry{
                              .channel_name = std::move(channel_name),
                              .message_encoding = MessageEncoding::undefined,
                            })
                          .first;
  channel_metadata_map_.emplace(
    map_iter->second.channel_name,
    ChannelMetadata{
      .channel_name = map_iter->second.channel_name,
      .compression_type = CompressionType::none,
      .message_encoding = map_iter->second.message_encoding,
      .schema_id = 0U,
      .channel_type = ChannelType::regular,
    });
}

template <typename BufferedReaderType>
[[nodiscard]] ZeroCopyLoggedMessage Reader<BufferedReaderType>::to_zero_copy_logged_message(
  std::string_view channel_name, const SavedPersistentMessage& message, LogTimestamp message_time)
{
  return ZeroCopyLoggedMessage{
    .channel_name = channel_name,
    .sequence_number = message.sequence_number,
    .log_time = message.log_time,
    .message_time = message_time,
    .header = std::span{message.header},
    .data = std::span{&message.data_span, 1U},
    .message_type = message.message_type,
    .message_encoding = message.message_encoding,
    .is_lite_compressed = message.is_lite_compressed,
  };
}

} // namespace clockwork_logging::onboard
