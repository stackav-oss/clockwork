// IWYU pragma: private, include "clockwork/logging/onboard/writer.hh"

#pragma once

#include "clockwork/logging/onboard/writer.hh"

#include "clockwork/logging/channel_type.hh"
#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/lite_compressor.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/message_encoding.hh"
#include "clockwork/logging/nolint_helper.hh"
#include "clockwork/logging/onboard/clockwork_message_handle.hh"
#include "clockwork/logging/onboard/log_format.hh"
#include "clockwork/logging/onboard/null_message_handle.hh"
#include "clockwork/logging/onboard/types.hh"
#include "clockwork/logging/onboard/writer_state.hh"
#include "clockwork/logging/schema_encoding.hh"
#include "clockwork/logging/xxh3_checksum.hh"
#include "clockwork/logging/zstd_helper.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/slot.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/math/constants.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/std/span.hh"
#include "jewels/time/sync_time.hh"

#include <fmt10/base.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iterator>
#include <list>
#include <memory_resource>
#include <mutex>
#include <numeric>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace clockwork_logging::onboard
{

template <typename Policy>
Writer<Policy>::GuardedState::GuardedState(jewels::memory::MemoryResource memory_resource)
  : message_rate_filter(memory_resource, jewels::time::SteadyClock::now(), rate_filter_window_sec),
    data_rate_filter(memory_resource, jewels::time::SteadyClock::now(), rate_filter_window_sec)
{
}

template <typename Policy>
Writer<Policy>::Writer(
  jewels::memory::MemoryResource init_memory_resource,
  jewels::memory::MemoryResource runtime_memory_resource,
  size_t max_write_mib_per_sec,
  std::chrono::nanoseconds max_log_file_duration,
  WriterEnvironment writer_environment)
  : guarded_state_(jewels::memory::make_pmr_unique<GuardedState>(init_memory_resource, init_memory_resource)),
    async_write_request_pool_(init_memory_resource, max_async_requests),
    write_buffer_pool_ptr_(
      jewels::memory::allocate_shared<BufferPoolType, std::pmr::polymorphic_allocator<BufferPoolType>>(
        init_memory_resource,
        init_memory_resource,
        calculate_write_buffer_pool_size(max_write_mib_per_sec, max_write_backlog))),
    async_writer_(init_memory_resource, runtime_memory_resource, writer_environment),
    runtime_memory_resource_(std::move(runtime_memory_resource)),
    max_log_file_duration_(max_log_file_duration),
    schema_metadata_list_(runtime_memory_resource_),
    channel_metadata_list_(runtime_memory_resource_),
    schema_map_(runtime_memory_resource_),
    channel_map_(runtime_memory_resource_),
    persistent_channel_message_map_(runtime_memory_resource_),
    compressor_(runtime_memory_resource_),
    writer_environment_(writer_environment)
{
}

template <typename Policy>
Writer<Policy>::Writer(
  jewels::memory::MemoryResource init_memory_resource,
  jewels::memory::MemoryResource runtime_memory_resource,
  jewels::memory::NonNullSharedPtr<BufferPoolType> buffer_pool_ptr,
  std::chrono::nanoseconds max_log_file_duration,
  WriterEnvironment writer_environment)
  : guarded_state_(jewels::memory::make_pmr_unique<GuardedState>(init_memory_resource, init_memory_resource)),
    async_write_request_pool_(init_memory_resource, max_async_requests),
    write_buffer_pool_ptr_(std::move(buffer_pool_ptr)),
    async_writer_(init_memory_resource, runtime_memory_resource, writer_environment),
    runtime_memory_resource_(std::move(runtime_memory_resource)),
    max_log_file_duration_(max_log_file_duration),
    schema_metadata_list_(runtime_memory_resource_),
    channel_metadata_list_(runtime_memory_resource_),
    schema_map_(runtime_memory_resource_),
    channel_map_(runtime_memory_resource_),
    persistent_channel_message_map_(runtime_memory_resource_),
    compressor_(runtime_memory_resource_),
    writer_environment_(writer_environment)
{
}

template <typename Policy>
Writer<Policy>::~Writer() noexcept
{
  if (async_request_handle_.is_valid() && async_request_handle_->get_write_size() != 0U)
  {
    jewels::log_cerr_error("Closing writer with unflushed data");
  }
}

template <typename Policy>
[[nodiscard]] LogExpected<void> Writer<Policy>::open_log(
  std::string_view log_path, std::string_view log_file_prefix, jewels::time::SteadyTime current_steady_time)
{
  if (get_state() == WriterState::failed)
  {
    return jewels::unexpected{
      guarded_state_->maybe_writer_error.load(std::memory_order_relaxed).value_or(LogError::failed)};
  }
  if (get_state() != WriterState::closed)
  {
    return jewels::unexpected(LogError::already_open);
  }
  if (const auto open_result = async_writer_.open_log(log_path, log_file_prefix); !open_result)
  {
    return open_result;
  }
  LogHeader log_header{};
  std::span<const std::byte> header_span = std::as_bytes(std::span{&log_header, 1U});
  auto open_result = copy_log_data({&header_span, 1U}, current_steady_time);
  if (const auto write_result = write_all_metadata(current_steady_time); !write_result && open_result)
  {
    open_result = write_result;
  }
  if (const auto write_result = write_latest_persistent_channel_messages(current_steady_time);
      !write_result && open_result)
  {
    open_result = write_result;
  }
  return open_result;
}

template <typename Policy>
[[nodiscard]] LogExpected<void>
Writer<Policy>::open_log_paused(std::string_view log_path, std::string_view log_file_prefix)
{
  const auto state = get_state();
  if (state == WriterState::failed)
  {
    return jewels::unexpected{
      guarded_state_->maybe_writer_error.load(std::memory_order_relaxed).value_or(LogError::failed)};
  }
  if (state != WriterState::closed)
  {
    return jewels::unexpected(LogError::already_open);
  }
  return async_writer_.open_log_paused(log_path, log_file_prefix);
}

template <typename Policy>
[[nodiscard]] LogExpected<void> Writer<Policy>::pause_logging(jewels::time::SteadyTime current_steady_time)
{
  const auto state = get_state();
  if (state == WriterState::failed)
  {
    return jewels::unexpected{
      guarded_state_->maybe_writer_error.load(std::memory_order_relaxed).value_or(LogError::failed)};
  }
  if (state == WriterState::paused)
  {
    return jewels::unexpected{LogError::paused};
  }
  if (state != WriterState::logging && state != WriterState::degraded)
  {
    return jewels::unexpected(LogError::not_open);
  }
  if (const auto end_log_result = write_end_log_file_record(current_steady_time); !end_log_result)
  {
    return jewels::unexpected(end_log_result.error());
  }
  if (async_request_handle_.is_valid())
  {
    if (const auto write_result = async_writer_.write_async(std::move(async_request_handle_)); !write_result)
    {
      guarded_state_->maybe_writer_error.store(LogError::async_flush_failed, std::memory_order_release);
      return write_result;
    }
    async_request_handle_ = {};
    guarded_state_->maybe_oldest_pending_data_timestamp.store(std::nullopt, std::memory_order_release);
  }
  return async_writer_.pause_logging();
}

template <typename Policy>
[[nodiscard]] LogExpected<void> Writer<Policy>::resume_logging(jewels::time::SteadyTime current_steady_time)
{
  if (const auto resume_result = async_writer_.resume_logging(); !resume_result)
  {
    return resume_result;
  }
  LogHeader log_header{};
  std::span<const std::byte> header_span = std::as_bytes(std::span{&log_header, 1U});
  auto resume_result = copy_log_data({&header_span, 1U}, current_steady_time);
  if (const auto write_result = write_all_metadata(current_steady_time); !write_result && resume_result)
  {
    resume_result = write_result;
  }
  if (const auto write_result = write_latest_persistent_channel_messages(current_steady_time);
      !write_result && resume_result)
  {
    resume_result = write_result;
  }
  return resume_result;
}

template <typename Policy>
[[nodiscard]] LogExpected<void> Writer<Policy>::close_log(jewels::time::SteadyTime current_steady_time)
{
  const auto state = get_state();
  if (state == WriterState::failed)
  {
    return jewels::unexpected{
      guarded_state_->maybe_writer_error.load(std::memory_order_relaxed).value_or(LogError::failed)};
  }
  if (state == WriterState::closed)
  {
    return jewels::unexpected(LogError::not_open);
  }
  if (state != WriterState::paused)
  {
    if (const auto pause_result = pause_logging(current_steady_time); !pause_result)
    {
      if (const auto close_result = async_writer_.close_log(); !close_result)
      {
        jewels::log_cerr_error("Failed to close the async writer during error handling: {}", close_result.error());
      }
      return pause_result;
    }
  }
  return async_writer_.close_log();
}

template <typename Policy>
[[nodiscard]] LogExpected<void> Writer<Policy>::add_channel_impl(
  const LoggedChannelMetadata& channel_metadata, jewels::time::SteadyTime current_steady_time)
{
  if (channel_metadata.compression_type != CompressionType::none)
  {
    return jewels::unexpected(LogError::unsupported_compression_type);
  }
  auto schema_encoding = channel_metadata.schema_encoding;
  auto schema_definition = channel_metadata.schema_definition;
  LogExpected<std::pmr::vector<std::byte>> compress_result;
  if (schema_encoding == SchemaEncoding::clockwork_tachyon)
  {
    compress_result = zstd_compress(
      std::as_bytes(std::span{schema_definition.data(), schema_definition.size()}), runtime_memory_resource_);
    if (!compress_result)
    {
      std::pmr::string status_string{runtime_memory_resource_};
      fmt::format_to(
        std::back_inserter(status_string),
        "Failed to compress schema definition for {}: {}",
        channel_metadata.channel_name,
        compress_result.error());
      jewels::log_cerr_error("{}", status_string);
      return jewels::unexpected(set_writer_error(compress_result.error(), std::move(status_string)));
    }
    schema_definition = nolint_helper::byte_span_to_string_view(compress_result.value()),
    schema_encoding = SchemaEncoding::clockwork_tachyon_zstd;
  }
  LogExpected<void> add_result{};
  uint16_t schema_id{0U};
  if (!channel_metadata.schema_name.empty())
  {
    if (const auto schema_map_iter = schema_map_.find(channel_metadata.schema_name);
        schema_map_iter != schema_map_.end())
    {
      schema_id = schema_map_iter->second->schema_id;
    }
    else
    {
      auto schema_ptr = add_schema_metadata(channel_metadata.schema_name, schema_encoding, schema_definition);
      schema_id = schema_ptr->schema_id;
      if (get_state() == WriterState::logging || get_state() == WriterState::degraded)
      {
        if (const auto write_result = write_schema_metadata(*schema_ptr, current_steady_time);
            !write_result && add_result)
        {
          add_result = write_result;
        }
      }
    }
  }
  auto channel_ptr = add_channel_metadata(
    channel_metadata.channel_name,
    channel_metadata.compression_type,
    channel_metadata.message_encoding,
    channel_metadata.channel_type,
    schema_id);
  if (get_state() == WriterState::logging || get_state() == WriterState::degraded)
  {
    if (const auto write_result = write_channel_metadata(*channel_ptr, current_steady_time);
        !write_result && add_result)
    {
      add_result = write_result;
    }
  }
  return add_result;
}

template <typename Policy>
[[nodiscard]] LogExpected<void>
Writer<Policy>::add_channel(const LoggedChannelMetadata& channel_metadata, jewels::time::SteadyTime current_steady_time)
{
  const auto state = get_state();
  if (state == WriterState::failed)
  {
    return jewels::unexpected{
      guarded_state_->maybe_writer_error.load(std::memory_order_relaxed).value_or(LogError::failed)};
  }
  if (channel_map_.contains(channel_metadata.channel_name))
  {
    return {};
  }
  if (channel_metadata.schema_name.size() > max_name_string_size)
  {
    std::pmr::string status_string{runtime_memory_resource_};
    fmt::format_to(std::back_inserter(status_string), "Schema name exceeds max size ({})", max_name_string_size);
    jewels::log_cerr_error("{}", status_string);
    return jewels::unexpected(set_writer_error(LogError::schema_name_exceeds_max_name_size, std::move(status_string)));
  }
  if (channel_metadata.channel_name.size() > max_name_string_size)
  {
    std::pmr::string status_string{runtime_memory_resource_};
    fmt::format_to(std::back_inserter(status_string), "Channel name exceeds max size ({})", max_name_string_size);
    jewels::log_cerr_error("{}", status_string);
    return jewels::unexpected(set_writer_error(LogError::channel_name_exceeds_max_name_size, std::move(status_string)));
  }
  if (channel_metadata.schema_definition.size() > max_schema_definition_string_size)
  {
    std::pmr::string status_string{runtime_memory_resource_};
    fmt::format_to(
      std::back_inserter(status_string), "Schema definition exceeds max size ({})", max_schema_definition_string_size);
    jewels::log_cerr_error("{}", status_string);
    return jewels::unexpected(set_writer_error(LogError::schema_definition_exceeds_max_size, std::move(status_string)));
  }
  return add_channel_impl(channel_metadata, current_steady_time);
}

template <typename Policy>
[[nodiscard]] LogExpected<void> Writer<Policy>::log_message(
  const Message& message, const MessageHandleType& message_handle, jewels::time::SteadyTime current_steady_time)
  requires(!std::is_same_v<MessageHandleType, ClockworkMessageHandle>)
{
  if (const auto check_result = pre_write_check(message.channel_name, current_steady_time); !check_result)
  {
    return check_result;
  }
  return log_message_impl(message, message_handle, current_steady_time);
}

template <typename Policy>
[[nodiscard]] LogExpected<void> Writer<Policy>::log_message_wait(
  const Message& message, const MessageHandleType& message_handle, jewels::time::SteadyTime current_steady_time)
  requires(!std::is_same_v<MessageHandleType, ClockworkMessageHandle>)
{
  if (const auto wait_result = pre_write_wait(); !wait_result)
  {
    return wait_result;
  }
  return log_message_impl(message, message_handle, current_steady_time);
}

template <typename Policy>
[[nodiscard]] LogExpected<void>
Writer<Policy>::save_persistent_message(const Message& message, const MessageHandleType& message_handle)
{
  const auto state = get_state();
  if (state == WriterState::failed)
  {
    return jewels::unexpected{
      guarded_state_->maybe_writer_error.load(std::memory_order_relaxed).value_or(LogError::failed)};
  }
  if (state == WriterState::logging || state == WriterState::degraded)
  {
    return jewels::unexpected(LogError::is_logging);
  }
  MessageRecordHeader record_header{};
  if (const auto header_result = fill_message_record_header(
        ZeroCopyMessage{
          .channel_name = message.channel_name,
          .sequence_number = message.sequence_number,
          .log_time = message.log_time,
          .message_time = message.message_time,
          .header = message.header,
          .data = std::span{&message.data, 1U},
        },
        /*is_lite_compressed=*/false,
        record_header);
      !header_result)
  {
    return jewels::unexpected(header_result.error());
  }
  if (record_header.flags.is_persistent == 0U)
  {
    return jewels::unexpected(LogError::not_persistent);
  }
  auto persistent_channel_message = copy_persistent_channel_message(record_header, message.header, message.data);
  if (!message_handle.is_valid())
  {
    if (guarded_state_->drop_count.fetch_add(1U, std::memory_order_release) == 0U)
    {
      jewels::log_cerr_warn("Buffer overrun detected, dropping message on channel {}", message.channel_name);
    }
    return jewels::unexpected(LogError::message_dropped);
  }
  persistent_channel_message_map_[message.channel_name] = std::move(persistent_channel_message);
  return {};
}

template <typename Policy>
[[nodiscard]] LogExpected<void> Writer<Policy>::log_clockwork_message(
  std::string_view channel_name,
  const ClockworkMessageHandle& message_handle,
  LogTimestamp log_time,
  jewels::time::SteadyTime current_steady_time)
{
  if (const auto check_result = pre_write_check(channel_name, current_steady_time); !check_result)
  {
    return check_result;
  }
  return log_clockwork_message_impl(channel_name, message_handle, log_time, current_steady_time);
}

template <typename Policy>
[[nodiscard]] LogExpected<void> Writer<Policy>::log_clockwork_message_wait(
  std::string_view channel_name,
  const ClockworkMessageHandle& message_handle,
  LogTimestamp log_time,
  jewels::time::SteadyTime current_steady_time)
{
  if (const auto wait_result = pre_write_wait(); !wait_result)
  {
    return wait_result;
  }
  return log_clockwork_message_impl(channel_name, message_handle, log_time, current_steady_time);
}

template <typename Policy>
[[nodiscard]] LogExpected<void> Writer<Policy>::save_persistent_clockwork_message(
  std::string_view channel_name, const ClockworkMessageHandle& message_handle, LogTimestamp log_time)
  requires std::is_same_v<MessageHandleType, NullMessageHandle>
{
  const auto state = get_state();
  if (state == WriterState::failed)
  {
    return jewels::unexpected{
      guarded_state_->maybe_writer_error.load(std::memory_order_relaxed).value_or(LogError::failed)};
  }
  if (state == WriterState::logging || state == WriterState::degraded)
  {
    return jewels::unexpected(LogError::is_logging);
  }
  auto slot = message_handle.get_buffer_iterator().dereference();
  auto header_ptr = slot.header();
  const std::span<const std::byte> message = slot.message();
  auto data = compressor_.compress(message);
  auto compressed_data_size =
    std::accumulate(data.begin(), data.end(), size_t{0U}, [](size_t lhs, auto& rhs) { return lhs + rhs.size(); });
  const bool is_lite_compressed = compressed_data_size < message.size();
  if (!is_lite_compressed)
  {
    data = std::span{&message, 1U};
  }
  MessageRecordHeader record_header{};
  if (const auto header_result = fill_message_record_header(
        ZeroCopyMessage{
          .channel_name = channel_name,
          .sequence_number = static_cast<uint32_t>(header_ptr->sequence_number),
          .log_time = log_time,
          .message_time = LogTimestamp{header_ptr->publish_timestamp},
          .header = {},
          .data = data,
        },
        is_lite_compressed,
        record_header);
      !header_result)
  {
    return jewels::unexpected(header_result.error());
  }
  if (record_header.flags.is_persistent == 0U)
  {
    return jewels::unexpected(LogError::not_persistent);
  }
  auto persistent_channel_message = copy_persistent_channel_message(record_header, {}, data);
  if (!message_handle.is_valid())
  {
    if (guarded_state_->drop_count.fetch_add(1U, std::memory_order_release) == 0U)
    {
      jewels::log_cerr_warn("Buffer overrun detected, dropping message on channel {}", channel_name);
    }
    return jewels::unexpected(LogError::message_dropped);
  }
  persistent_channel_message_map_[channel_name] = std::move(persistent_channel_message);
  return {};
}

template <typename Policy>
[[nodiscard]] LogExpected<void> Writer<Policy>::log_message(
  const Message& message, bool is_lite_compressed, jewels::time::SteadyTime current_steady_time)
{
  return log_message(
    ZeroCopyMessage{
      .channel_name = message.channel_name,
      .sequence_number = message.sequence_number,
      .log_time = message.log_time,
      .message_time = message.message_time,
      .header = message.header,
      .data = std::span{&message.data, 1U},
    },
    is_lite_compressed,
    current_steady_time);
}

template <typename Policy>
[[nodiscard]] LogExpected<void> Writer<Policy>::log_message(
  const ZeroCopyMessage& message, bool is_lite_compressed, jewels::time::SteadyTime current_steady_time)
{
  if (const auto check_result = pre_write_check(message.channel_name, current_steady_time); !check_result)
  {
    return check_result;
  }
  return log_message_impl(message, is_lite_compressed, current_steady_time);
}

template <typename Policy>
[[nodiscard]] LogExpected<void> Writer<Policy>::log_message_wait(
  const Message& message, bool is_lite_compressed, jewels::time::SteadyTime current_steady_time)
{
  return log_message_wait(
    ZeroCopyMessage{
      .channel_name = message.channel_name,
      .sequence_number = message.sequence_number,
      .log_time = message.log_time,
      .message_time = message.message_time,
      .header = message.header,
      .data = std::span{&message.data, 1U},
    },
    is_lite_compressed,
    current_steady_time);
}

template <typename Policy>
[[nodiscard]] LogExpected<void> Writer<Policy>::log_message_wait(
  const ZeroCopyMessage& message, bool is_lite_compressed, jewels::time::SteadyTime current_steady_time)
{
  if (const auto wait_result = pre_write_wait(); !wait_result)
  {
    return wait_result;
  }
  return log_message_impl(message, is_lite_compressed, current_steady_time);
}

template <typename Policy>
[[nodiscard]] LogExpected<void> Writer<Policy>::save_persistent_message(const Message& message, bool is_lite_compressed)
{
  return save_persistent_message(
    ZeroCopyMessage{
      .channel_name = message.channel_name,
      .sequence_number = message.sequence_number,
      .log_time = message.log_time,
      .message_time = message.message_time,
      .header = message.header,
      .data = std::span{&message.data, 1U},
    },
    is_lite_compressed);
}

template <typename Policy>
[[nodiscard]] LogExpected<void>
Writer<Policy>::save_persistent_message(const ZeroCopyMessage& message, bool is_lite_compressed)
{
  const auto state = get_state();
  if (state == WriterState::failed)
  {
    return jewels::unexpected{
      guarded_state_->maybe_writer_error.load(std::memory_order_relaxed).value_or(LogError::failed)};
  }
  if (state == WriterState::logging || state == WriterState::degraded)
  {
    return jewels::unexpected(LogError::is_logging);
  }
  MessageRecordHeader record_header{};
  if (const auto header_result = fill_message_record_header(message, is_lite_compressed, record_header); !header_result)
  {
    return jewels::unexpected(header_result.error());
  }
  if (record_header.flags.is_persistent == 0U)
  {
    return jewels::unexpected(LogError::not_persistent);
  }
  auto persistent_channel_message = copy_persistent_channel_message(record_header, message.header, message.data);
  persistent_channel_message_map_[message.channel_name] = std::move(persistent_channel_message);
  return {};
}

template <typename Policy>
[[nodiscard]] LogExpected<void>
Writer<Policy>::pre_write_check(std::string_view channel_name, jewels::time::SteadyTime current_steady_time)
{
  if (writer_environment_ != WriterEnvironment::simulation)
  {
    const auto backlog_result = get_write_backlog(current_steady_time);
    if (!backlog_result)
    {
      return jewels::unexpected(backlog_result.error());
    }
    if (*backlog_result > max_write_backlog)
    {
      if (guarded_state_->drop_count.fetch_add(1U, std::memory_order_release) == 0U)
      {
        jewels::log_cerr_warn("Max write backlog exceeded, dropping message on channel {}", channel_name);
      }
      return jewels::unexpected(LogError::message_dropped);
    }
  }
  if (
    (!std::is_same_v<MessageHandleType, ClockworkMessageHandle> &&
     write_buffer_pool_ptr_->get_avail_count() <= calculate_schema_reserve_buffers()) ||
    (async_writer_.get_avail_async_request_handles() <= calculate_schema_reserve_async_write_requests()))
  {
    if (guarded_state_->drop_count.fetch_add(1U, std::memory_order_release) == 0U)
    {
      jewels::log_cerr_warn("Write rate exceeds configured maximum, dropping message on channel {}", channel_name);
    }
    return jewels::unexpected(LogError::message_dropped);
  }
  return {};
}

template <typename Policy>
[[nodiscard]] LogExpected<void> Writer<Policy>::pre_write_wait()
{
  auto pending_request_result = async_writer_.get_pending_request_count();
  const auto min_free_buffers =
    (write_buffer_pool_ptr_->get_capacity() - calculate_write_buffer_pool_size(0U, std::chrono::nanoseconds(0))) / 2U;
  while ((pending_request_result && (*pending_request_result > (max_async_requests / 2U))) ||
         (write_buffer_pool_ptr_->get_avail_count() < min_free_buffers))
  {
    std::this_thread::sleep_for(drain_sleep_time);
    periodic_callback(jewels::time::SteadyClock::now());
    pending_request_result = async_writer_.get_pending_request_count();
  }
  if (!pending_request_result)
  {
    return jewels::unexpected(pending_request_result.error());
  }
  return {};
}

template <typename Policy>
[[nodiscard]] LogExpected<void> Writer<Policy>::fill_message_record_header(
  const ZeroCopyMessage& message, bool is_lite_compressed, MessageRecordHeader& record_header)
  requires(!std::is_same_v<MessageHandleType, ClockworkMessageHandle>)
{
  const auto data_size = std::accumulate(
    message.data.begin(), message.data.end(), size_t{0U}, [](size_t lhs, auto& rhs) { return lhs + rhs.size(); });
  if (message.header.size() > max_message_header_size)
  {
    std::pmr::string status_string{"Message header exceeds max size", runtime_memory_resource_};
    jewels::log_cerr_error("{}", status_string);
    return jewels::unexpected(set_writer_error(LogError::message_header_exceeds_max_size, std::move(status_string)));
  }
  const auto record_size = message_record_header_size + message.header.size() + data_size + sizeof(RecordTrailer);
  if (record_size > max_log_record_size)
  {
    std::pmr::string status_string{"Message record length exceeds max size", runtime_memory_resource_};
    jewels::log_cerr_error("{}", status_string);
    return jewels::unexpected(
      set_writer_error(LogError::record_length_exceeds_max_record_size, std::move(status_string)));
  }
  const auto channel_map_iter = channel_map_.find(message.channel_name);
  if (channel_map_iter == channel_map_.end())
  {
    std::pmr::string status_string{runtime_memory_resource_};
    fmt::format_to(
      std::back_inserter(status_string), "Channel metadata for {} is not configured", message.channel_name);
    jewels::log_cerr_error("{}", status_string);
    return jewels::unexpected(set_writer_error(LogError::missing_channel_metadata, std::move(status_string)));
  }
  const auto channel_id = channel_map_iter->second->channel_id;
  record_header.header.record_type = RecordType::message;
  record_header.header.record_size = static_cast<uint32_t>(record_size);
  record_header.channel_id = channel_id;
  record_header.sequence_number = message.sequence_number;
  record_header.log_time_ns = message.log_time.get_nanoseconds();
  record_header.message_time_ns = message.message_time.get_nanoseconds();
  const auto is_persistent = channel_map_iter->second->channel_type == ChannelType::persistent;
  record_header.flags.is_persistent = is_persistent ? 1U : 0U;
  record_header.flags.is_lite_compressed = (is_lite_compressed ? 1U : 0U);
  record_header.header_length = static_cast<uint16_t>(message.header.size());
  return {};
}

template <typename Policy>
[[nodiscard]] LogExpected<void> Writer<Policy>::log_message_impl(
  const Message& message, const MessageHandleType& message_handle, jewels::time::SteadyTime current_steady_time)
  requires(!std::is_same_v<MessageHandleType, ClockworkMessageHandle>)
{
  MessageRecordHeader record_header{};
  if (const auto header_result = fill_message_record_header(
        ZeroCopyMessage{
          .channel_name = message.channel_name,
          .sequence_number = message.sequence_number,
          .log_time = message.log_time,
          .message_time = message.message_time,
          .header = message.header,
          .data = std::span{&message.data, 1U},
        },
        /*is_lite_compressed=*/false,
        record_header);
      !header_result)
  {
    return jewels::unexpected(header_result.error());
  }
  RecordTrailer record_trailer{};
  std::array record_spans = {
    std::as_bytes(std::span{&record_header, 1U}),
    message.header,
    message.data,
    std::as_bytes(std::span{&record_trailer, 1U})};
  record_trailer.xxh3_checksum = compute_xxh3_checksum(std::span{record_spans}.first(record_spans.size() - 1U));
  std::optional<PersistentChannelMessage> maybe_persistent_channel_message;
  if (record_header.flags.is_persistent == 1U)
  {
    maybe_persistent_channel_message = copy_persistent_channel_message(record_header, message.header, message.data);
  }
  if (!message_handle.is_valid())
  {
    if (guarded_state_->drop_count.fetch_add(1U, std::memory_order_release) == 0U)
    {
      jewels::log_cerr_warn("Buffer overrun detected, dropping message on channel {}", message.channel_name);
    }
    return jewels::unexpected(LogError::message_dropped);
  }
  if (maybe_persistent_channel_message)
  {
    persistent_channel_message_map_[message.channel_name] = std::move(maybe_persistent_channel_message).value();
  }
  update_logging_rates(record_header.header.record_size, 1U);
  auto write_result =
    message.data.size() >= Policy::min_zero_copy_message_size && message_handle.supports_zero_copy()
      ? zero_copy_log_message(
          record_header, message.header, message.data, record_trailer, std::move(message_handle), current_steady_time)
      : copy_log_data({record_spans}, current_steady_time);
  if (!write_result)
  {
    if (get_state() == WriterState::failed)
    {
      return jewels::unexpected{
        guarded_state_->maybe_writer_error.load(std::memory_order_relaxed).value_or(LogError::failed)};
    }
  }
  if (!record_header.flags.is_repeated)
  {
    if (const auto split_result = split_log_if_needed(message.message_time, message.log_time, current_steady_time);
        !split_result && write_result)
    {
      return split_result;
    }
  }
  return write_result;
}

template <typename Policy>
[[nodiscard]] LogExpected<void> Writer<Policy>::log_clockwork_message_impl(
  std::string_view channel_name,
  const ClockworkMessageHandle& message_handle,
  LogTimestamp log_time,
  jewels::time::SteadyTime current_steady_time)
  requires std::is_same_v<MessageHandleType, NullMessageHandle>
{
  auto slot = message_handle.get_buffer_iterator().dereference();
  auto header_ptr = slot.header();
  const std::span<const std::byte> message = slot.message();
  auto data = compressor_.compress(message);
  auto compressed_data_size =
    std::accumulate(data.begin(), data.end(), size_t{0U}, [](size_t lhs, auto& rhs) { return lhs + rhs.size(); });
  const auto is_lite_compressed = compressed_data_size < message.size();
  if (!is_lite_compressed)
  {
    data = std::span{&message, 1U};
  }
  MessageRecordHeader record_header{};
  if (const auto header_result = fill_message_record_header(
        ZeroCopyMessage{
          .channel_name = channel_name,
          .sequence_number = static_cast<uint32_t>(header_ptr->sequence_number),
          .log_time = log_time,
          .message_time = LogTimestamp{header_ptr->publish_timestamp},
          .header = {},
          .data = data,
        },
        is_lite_compressed,
        record_header);
      !header_result)
  {
    return jewels::unexpected(header_result.error());
  }
  RecordTrailer record_trailer;
  std::pmr::vector<std::span<const std::byte>> record_spans{runtime_memory_resource_};
  record_spans.reserve(data.size() + 2U);
  record_spans.emplace_back(std::as_bytes(std::span{&record_header, 1U}));
  record_spans.insert(record_spans.end(), data.begin(), data.end());
  record_spans.emplace_back(std::as_bytes(std::span{&record_trailer, 1U}));
  record_trailer.xxh3_checksum = compute_xxh3_checksum({std::span{record_spans}.first(record_spans.size() - 1U)});
  std::optional<PersistentChannelMessage> maybe_persistent_channel_message;
  if (record_header.flags.is_persistent == 1U)
  {
    maybe_persistent_channel_message = copy_persistent_channel_message(record_header, {}, data);
  }
  if (!message_handle.is_valid())
  {
    if (guarded_state_->drop_count.fetch_add(1U, std::memory_order_release) == 0U)
    {
      jewels::log_cerr_warn("Buffer overrun detected, dropping message on channel {}", channel_name);
    }
    return jewels::unexpected(LogError::message_dropped);
  }
  if (maybe_persistent_channel_message)
  {
    persistent_channel_message_map_[channel_name] = std::move(maybe_persistent_channel_message).value();
  }
  update_logging_rates(record_header.header.record_size, 1U);
  const auto copy_result = copy_log_data({record_spans}, current_steady_time);
  if (!copy_result && (get_state() == WriterState::failed))
  {
    return jewels::unexpected{
      guarded_state_->maybe_writer_error.load(std::memory_order_relaxed).value_or(LogError::failed)};
  }
  const auto message_time = LogTimestamp{record_header.message_time_ns};
  if (const auto split_result = split_log_if_needed(message_time, log_time, current_steady_time);
      !split_result && copy_result)
  {
    return split_result;
  }
  return copy_result;
}

template <typename Policy>
[[nodiscard]] LogExpected<void> Writer<Policy>::log_message_impl(
  const ZeroCopyMessage& message, bool is_lite_compressed, jewels::time::SteadyTime current_steady_time)
{
  MessageRecordHeader record_header{};
  if (const auto header_result = fill_message_record_header(message, is_lite_compressed, record_header); !header_result)
  {
    return jewels::unexpected(header_result.error());
  }
  RecordTrailer record_trailer{};
  std::pmr::vector<std::span<const std::byte>> record_spans{runtime_memory_resource_};
  record_spans.reserve(message.data.size() + 3U);
  record_spans.emplace_back(std::as_bytes(std::span{&record_header, 1U}));
  record_spans.emplace_back(message.header);
  record_spans.insert(record_spans.end(), message.data.begin(), message.data.end());
  record_spans.emplace_back(std::as_bytes(std::span{&record_trailer, 1U}));
  record_trailer.xxh3_checksum =
    compute_xxh3_checksum(std::span{record_spans.data(), record_spans.size()}.first(record_spans.size() - 1U));
  std::optional<PersistentChannelMessage> maybe_persistent_channel_message;
  if (record_header.flags.is_persistent == 1U)
  {
    maybe_persistent_channel_message = copy_persistent_channel_message(record_header, message.header, message.data);
  }
  if (maybe_persistent_channel_message)
  {
    persistent_channel_message_map_[message.channel_name] = std::move(maybe_persistent_channel_message).value();
  }
  update_logging_rates(record_header.header.record_size, 1U);
  const auto write_result = copy_log_data({record_spans}, current_steady_time);
  if (!write_result)
  {
    if (get_state() == WriterState::failed)
    {
      return jewels::unexpected{
        guarded_state_->maybe_writer_error.load(std::memory_order_relaxed).value_or(LogError::failed)};
    }
  }
  if (const auto split_result = split_log_if_needed(message.message_time, message.log_time, current_steady_time);
      !split_result && write_result)
  {
    return split_result;
  }
  return write_result;
}

template <typename Policy>
void Writer<Policy>::periodic_callback(jewels::time::SteadyTime current_steady_time)
{
  if (
    async_request_handle_.is_valid() &&
    (current_steady_time - async_request_handle_->try_get_oldest_data_timestamp().value_or(current_steady_time)) >=
      flush_interval)
  {
    // Async writer sets its state to failed or degraded if this write fails
    if (const auto write_result = async_writer_.write_async(std::move(async_request_handle_)); !write_result)
    {
      guarded_state_->maybe_writer_error.store(LogError::async_flush_failed, std::memory_order_release);
    }
    async_request_handle_ = {};
    guarded_state_->maybe_oldest_pending_data_timestamp.store(std::nullopt, std::memory_order_release);
  }
  async_writer_.periodic_callback();
}

template <typename Policy>
[[nodiscard]] LogExpected<void> Writer<Policy>::drain_async_operations()
{
  if (async_request_handle_.is_valid())
  {
    if (const auto write_result = async_writer_.write_async(std::move(async_request_handle_)); !write_result)
    {
      guarded_state_->maybe_writer_error.store(LogError::async_flush_failed, std::memory_order_release);
      return write_result;
    }
    async_request_handle_ = {};
    guarded_state_->maybe_oldest_pending_data_timestamp.store(std::nullopt, std::memory_order_release);
  }
  return async_writer_.drain_async_operations();
}

template <typename Policy>
[[nodiscard]] LogExpected<std::chrono::nanoseconds>
Writer<Policy>::get_write_backlog(jewels::time::SteadyTime current_steady_time) const
{
  const auto backlog_result = async_writer_.get_write_backlog(current_steady_time);
  if (!backlog_result)
  {
    return backlog_result;
  }
  return std::max(
    backlog_result.value(),
    current_steady_time - guarded_state_->maybe_oldest_pending_data_timestamp.load(std::memory_order_acquire)
                            .value_or(current_steady_time));
}

template <typename Policy>
[[nodiscard]] WriterState Writer<Policy>::get_state() const
{
  const auto async_writer_state = async_writer_.get_state();
  const auto maybe_writer_error = guarded_state_->maybe_writer_error.load(std::memory_order_acquire);
  return (maybe_writer_error && (async_writer_state == WriterState::logging)) ? WriterState::degraded
                                                                              : async_writer_state;
}

template <typename Policy>
[[nodiscard]] WriterStatusResult Writer<Policy>::get_status()
{
  auto async_writer_status_string = async_writer_.get_status_string();
  const std::lock_guard guard(guarded_state_->mutex);
  const auto current_steady_time = jewels::time::SteadyClock::now();
  return WriterStatusResult{
    .status_string =
      async_writer_status_string.empty() ? guarded_state_->status_string : std::move(async_writer_status_string),
    .msgs_per_sec = guarded_state_->message_rate_filter.get_rate(current_steady_time),
    .bytes_per_sec = guarded_state_->data_rate_filter.get_rate(current_steady_time),
  };
}

template <typename Policy>
[[nodiscard]] LogExpected<size_t> Writer<Policy>::get_log_file_offset() const
{
  const auto offset_result = async_writer_.get_log_file_offset();
  if (!offset_result)
  {
    return offset_result;
  }
  auto file_offset = *offset_result;
  if (async_request_handle_.is_valid())
  {
    file_offset += async_request_handle_->get_write_size();
  }
  return file_offset;
}

template <typename Policy>
[[nodiscard]] typename Writer<Policy>::AsyncWriterType& Writer<Policy>::get_async_writer()
{
  return async_writer_;
}

template <typename Policy>
[[nodiscard]] size_t Writer<Policy>::get_and_reset_drop_count()
{
  return guarded_state_->drop_count.exchange(0U, std::memory_order_release) + async_writer_.get_and_reset_drop_count();
}

template <typename Policy>
[[nodiscard]] constexpr size_t Writer<Policy>::calculate_schema_reserve_buffers() noexcept
{
  return (schema_reserve_mib + persistent_message_reserve_mib) * jewels::math::constants::bytes_per_mib<size_t> /
         buffer_size;
}

template <typename Policy>
[[nodiscard]] constexpr size_t Writer<Policy>::calculate_schema_reserve_async_write_requests() noexcept
{
  return (schema_reserve_mib + persistent_message_reserve_mib) * jewels::math::constants::bytes_per_mib<size_t> /
         max_write_size;
}

template <typename Policy>
[[nodiscard]] size_t Writer<Policy>::calculate_write_buffer_pool_size(
  size_t max_write_mib_per_sec, std::chrono::nanoseconds max_backlog) noexcept
{
  if constexpr (std::is_same_v<MessageHandleType, ClockworkMessageHandle>)
  {
    return calculate_schema_reserve_buffers();
  }
  else
  {
    const auto max_buffers_per_sec =
      max_write_mib_per_sec * jewels::math::constants::bytes_per_mib<size_t> / buffer_size;
    constexpr std::chrono::nanoseconds one_second = std::chrono::seconds(1);
    const auto max_backlog_buffers =
      max_buffers_per_sec * static_cast<size_t>(max_backlog.count()) / static_cast<size_t>(one_second.count());
    return max_backlog_buffers + calculate_schema_reserve_buffers();
  }
}

template <typename Policy>
[[nodiscard]] LogExpected<void> Writer<Policy>::split_log(jewels::time::SteadyTime current_steady_time)
{
  if (const auto pause_result = pause_logging(current_steady_time); !pause_result)
  {
    return jewels::unexpected(pause_result.error());
  }
  return resume_logging(current_steady_time);
}

template <typename Policy>
[[nodiscard]] LogExpected<void> Writer<Policy>::zero_copy_log_message(
  const MessageRecordHeader& record_header,
  std::span<const std::byte> header,
  std::span<const std::byte> data,
  const RecordTrailer& record_trailer,
  const MessageHandleType& message_handle,
  jewels::time::SteadyTime current_steady_time)
  requires(!std::is_same_v<MessageHandleType, ClockworkMessageHandle>)
{
  const auto data_start_offset = static_cast<size_t>(AlignerType::ptr_aligned_remainder(data.data()));
  const auto data_end_offset = AlignerType::aligned_offset(data.size() - data_start_offset);
  const auto pre_aligned_size = message_record_header_size + header.size() + data_start_offset;
  if (!async_request_handle_.is_valid())
  {
    auto handle_result = async_write_request_pool_.make_shared_object();
    if (!handle_result)
    {
      std::pmr::string status_string{runtime_memory_resource_};
      fmt::format_to(
        std::back_inserter(status_string),
        "Failed to allocate an async write request handle: {}",
        handle_result.error());
      jewels::log_cerr_error("{}", status_string);
      return jewels::unexpected(set_writer_error(LogError::failed_to_allocate_async_request, std::move(status_string)));
    }
    async_request_handle_ = std::move(handle_result).value();
  }
  if (!async_request_handle_->pad_for_alignment(pre_aligned_size))
  {
    if (const auto advance_result = advance_to_next_buffer(); !advance_result)
    {
      return advance_result;
    }
    if (!async_request_handle_->pad_for_alignment(pre_aligned_size))
    {
      std::pmr::string status_string{"Failed to pad for zero copy write", runtime_memory_resource_};
      jewels::log_cerr_error("{}", status_string);
      return jewels::unexpected(set_writer_error(LogError::failed_to_pad_for_zero_copy, std::move(status_string)));
    }
  }
  std::array pre_aligned_spans = {std::as_bytes(std::span{&record_header, 1U}), header, data.first(data_start_offset)};
  if (const auto copy_result = copy_log_data({pre_aligned_spans}, current_steady_time); !copy_result)
  {
    return copy_result;
  }
  if (const auto copy_result = zero_copy_log_data(
        data.subspan(data_start_offset, data.size() - data_start_offset - data_end_offset),
        message_handle,
        current_steady_time);
      !copy_result)
  {
    return copy_result;
  }
  std::array post_aligned_spans = {data.last(data_end_offset), std::as_bytes(std::span{&record_trailer, 1U})};
  return copy_log_data({post_aligned_spans}, current_steady_time);
}

template <typename Policy>
[[nodiscard]] LogExpected<void>
Writer<Policy>::write_latest_persistent_channel_messages(jewels::time::SteadyTime current_steady_time)
{
  LogExpected<void> write_result;
  for (const auto& message : std::ranges::views::values(persistent_channel_message_map_))
  {
    std::array data_spans = {
      std::as_bytes(std::span{&message.record_header, 1U}),
      std::as_bytes(std::span{message.payload}),
      std::as_bytes(std::span{&message.record_trailer, 1U})};
    if (const auto copy_result = copy_log_data({data_spans}, current_steady_time); !copy_result)
    {
      if (write_result)
      {
        write_result = copy_result;
      }
    }
    else
    {
      update_logging_rates(message.record_header.header.record_size, 1U);
    }
  }
  return write_result;
}

template <typename Policy>
[[nodiscard]] LogExpected<void> Writer<Policy>::write_all_metadata(jewels::time::SteadyTime current_steady_time)
{
  LogExpected<void> write_result;
  std::ranges::for_each(
    schema_metadata_list_,
    [this, &write_result, current_steady_time](const auto& schema_metadata)
    {
      if (const auto schema_result = write_schema_metadata(schema_metadata, current_steady_time);
          !schema_result && write_result)
      {
        write_result = schema_result;
      }
    });
  std::ranges::for_each(
    channel_metadata_list_,
    [this, &write_result, current_steady_time](const auto& channel_metadata)
    {
      if (const auto channel_result = write_channel_metadata(channel_metadata, current_steady_time);
          !channel_result && write_result)
      {
        write_result = channel_result;
      }
    });
  return write_result;
}

template <typename Policy>
[[nodiscard]] jewels::memory::ObjectPtr<const typename Writer<Policy>::SchemaMetadata>
Writer<Policy>::add_schema_metadata(
  std::string_view schema_name, SchemaEncoding schema_encoding, std::string_view schema_definition)
{
  if (const auto map_iter = schema_map_.find(schema_name); map_iter != schema_map_.end())
  {
    return map_iter->second;
  }
  ++schema_count_;
  const auto& schema_metadata = schema_metadata_list_.emplace_back(
    SchemaMetadata{
      schema_count_,
      std::pmr::string{schema_name, runtime_memory_resource_},
      schema_encoding,
      std::pmr::string{schema_definition, runtime_memory_resource_}});
  const auto schema_ptr = jewels::memory::make_non_null_from_ref(schema_metadata);
  schema_map_.emplace(schema_ptr->schema_name, schema_ptr);
  return schema_ptr;
}

template <typename Policy>
[[nodiscard]] LogExpected<void> Writer<Policy>::write_schema_metadata(
  const SchemaMetadata& schema_metadata, jewels::time::SteadyTime current_steady_time)
{
  const auto record_size = sizeof(SchemaRecordHeader) + schema_metadata.schema_name.size() +
                           schema_metadata.schema_definition.size() + sizeof(RecordTrailer);
  if (record_size > max_log_record_size)
  {
    std::pmr::string status_string{"Schema record exceeds max size", runtime_memory_resource_};
    jewels::log_cerr_error("{}", status_string);
    return jewels::unexpected(
      set_writer_error(LogError::record_length_exceeds_max_record_size, std::move(status_string)));
  }
  SchemaRecordHeader header{};
  header.header.record_type = RecordType::schema;
  header.header.record_size = static_cast<uint32_t>(record_size);
  header.schema_id = schema_metadata.schema_id;
  header.schema_encoding = schema_metadata.schema_encoding;
  header.schema_name_length = static_cast<uint16_t>(schema_metadata.schema_name.size());
  RecordTrailer trailer{};
  std::array data_spans = {
    std::as_bytes(std::span{&header, 1U}),
    std::as_bytes(std::span{schema_metadata.schema_name.data(), schema_metadata.schema_name.size()}),
    std::as_bytes(std::span{schema_metadata.schema_definition.data(), schema_metadata.schema_definition.size()}),
    std::as_bytes(std::span{&trailer, 1U})};
  trailer.xxh3_checksum = compute_xxh3_checksum(std::span{data_spans}.first(data_spans.size() - 1U));
  update_logging_rates(record_size, 0U);
  return copy_log_data({data_spans}, current_steady_time);
}

template <typename Policy>
[[nodiscard]] jewels::memory::ObjectPtr<const typename Writer<Policy>::ChannelMetadata>
Writer<Policy>::add_channel_metadata(
  std::string_view channel_name,
  CompressionType compression_type,
  MessageEncoding message_encoding,
  ChannelType channel_type,
  uint16_t schema_id)
{
  if (const auto map_iter = channel_map_.find(channel_name); map_iter != channel_map_.end())
  {
    return map_iter->second;
  }
  ++channel_count_;
  const auto& channel_metadata = channel_metadata_list_.emplace_back(
    ChannelMetadata{
      channel_count_,
      schema_id,
      std::pmr::string{channel_name, runtime_memory_resource_},
      compression_type,
      message_encoding,
      channel_type});
  const auto channel_ptr = jewels::memory::make_non_null_from_ref(channel_metadata);
  channel_map_.emplace(channel_ptr->channel_name, channel_ptr);
  return channel_ptr;
}

template <typename Policy>
[[nodiscard]] LogExpected<void> Writer<Policy>::write_channel_metadata(
  const ChannelMetadata& channel_metadata, jewels::time::SteadyTime current_steady_time)
{
  const auto record_size = sizeof(ChannelRecordHeader) + channel_metadata.channel_name.size() + sizeof(RecordTrailer);
  if (record_size > max_log_record_size)
  {
    std::pmr::string status_string{"Channel record exceeds max size", runtime_memory_resource_};
    jewels::log_cerr_error("{}", status_string);
    return jewels::unexpected(
      set_writer_error(LogError::record_length_exceeds_max_record_size, std::move(status_string)));
  }
  ChannelRecordHeader header{};
  header.header.record_type = RecordType::channel;
  header.header.record_size = static_cast<uint32_t>(record_size);
  header.channel_id = channel_metadata.channel_id;
  header.schema_id = channel_metadata.schema_id;
  header.compression_type = channel_metadata.compression_type;
  header.message_encoding = channel_metadata.message_encoding;
  header.flags.is_persistent = channel_metadata.channel_type == ChannelType::persistent ? 1U : 0U;
  RecordTrailer trailer{};
  std::array data_spans{
    std::as_bytes(std::span{&header, 1U}),
    std::as_bytes(std::span{channel_metadata.channel_name.data(), channel_metadata.channel_name.size()}),
    std::as_bytes(std::span{&trailer, 1U})};
  trailer.xxh3_checksum = compute_xxh3_checksum(std::span{data_spans}.first(data_spans.size() - 1U));
  update_logging_rates(record_size, 0U);
  return copy_log_data({data_spans}, current_steady_time);
}

template <typename Policy>
[[nodiscard]] LogExpected<void> Writer<Policy>::write_end_log_file_record(jewels::time::SteadyTime current_steady_time)
{
  if (!async_request_handle_.is_valid())
  {
    auto handle_result = async_write_request_pool_.make_shared_object();
    if (!handle_result)
    {
      std::pmr::string status_string{runtime_memory_resource_};
      fmt::format_to(
        std::back_inserter(status_string),
        "Failed to allocate an async write request handle: {}",
        handle_result.error());
      jewels::log_cerr_error("{}", status_string);
      return jewels::unexpected(set_writer_error(LogError::failed_to_allocate_async_request, std::move(status_string)));
    }
    async_request_handle_ = std::move(handle_result).value();
  }
  const auto record_size = sizeof(EndLogFileRecordHeader) + sizeof(RecordTrailer);
  if (!async_request_handle_->pad_for_alignment(record_size))
  {
    if (const auto advance_result = advance_to_next_buffer(); !advance_result)
    {
      return advance_result;
    }
    if (!async_request_handle_->pad_for_alignment(record_size))
    {
      std::pmr::string status_string{"Failed to pad for end log file record", runtime_memory_resource_};
      jewels::log_cerr_error("{}", status_string);
      return jewels::unexpected(
        set_writer_error(LogError::failed_to_write_end_log_file_record, std::move(status_string)));
    }
  }
  EndLogFileRecordHeader header{};
  header.header.record_type = RecordType::end_log_file;
  header.header.record_size = static_cast<uint32_t>(record_size);
  header.min_log_time_ns = maybe_min_log_timestamp_.value_or(LogTimestamp{0}).get_nanoseconds();
  header.max_log_time_ns = maybe_max_log_timestamp_.value_or(LogTimestamp{0}).get_nanoseconds();
  header.min_message_time_ns = maybe_min_message_timestamp_.value_or(LogTimestamp{0}).get_nanoseconds();
  header.max_message_time_ns = maybe_max_message_timestamp_.value_or(LogTimestamp{0}).get_nanoseconds();
  header.has_messages = static_cast<bool>(maybe_min_log_timestamp_);
  RecordTrailer trailer{};
  std::array data_spans{std::as_bytes(std::span{&header, 1U}), std::as_bytes(std::span{&trailer, 1U})};
  trailer.xxh3_checksum = compute_xxh3_checksum(std::as_bytes(jewels::as_single_item_span(header)));
  maybe_min_log_timestamp_ = std::nullopt;
  maybe_max_log_timestamp_ = std::nullopt;
  maybe_min_message_timestamp_ = std::nullopt;
  maybe_max_message_timestamp_ = std::nullopt;
  update_logging_rates(record_size, 0U);
  return copy_log_data({data_spans}, current_steady_time);
}

template <typename Policy>
[[nodiscard]] LogExpected<void> Writer<Policy>::copy_log_data(
  std::span<std::span<const std::byte>> data_spans, jewels::time::SteadyTime current_steady_time)
{
  for (const auto data : data_spans)
  {
    if (const auto copy_result = copy_log_data(data, current_steady_time); !copy_result)
    {
      return copy_result;
    }
  }
  return {};
}

template <typename Policy>
[[nodiscard]] LogExpected<void>
Writer<Policy>::copy_log_data(std::span<const std::byte> data, jewels::time::SteadyTime current_steady_time)
{
  if (!async_request_handle_.is_valid())
  {
    auto handle_result = async_write_request_pool_.make_shared_object();
    if (!handle_result)
    {
      std::pmr::string status_string{runtime_memory_resource_};
      fmt::format_to(
        std::back_inserter(status_string),
        "Failed to allocate an async write request handle: {}",
        handle_result.error());
      jewels::log_cerr_error("{}", status_string);
      return jewels::unexpected(set_writer_error(LogError::failed_to_allocate_async_request, std::move(status_string)));
    }
    async_request_handle_ = std::move(handle_result).value();
  }
  size_t bytes_remaining = data.size();
  while (bytes_remaining != 0U)
  {
    const auto subspan = data.last(bytes_remaining);
    const auto bytes_copied = async_request_handle_->copy_data(current_steady_time, subspan);
    if (!guarded_state_->maybe_oldest_pending_data_timestamp.load(std::memory_order_relaxed).has_value())
    {
      guarded_state_->maybe_oldest_pending_data_timestamp.store(current_steady_time, std::memory_order_release);
    }
    bytes_remaining -= bytes_copied;
    if (bytes_copied < subspan.size())
    {
      if (const auto advance_result = advance_to_next_buffer(); !advance_result)
      {
        return advance_result;
      }
    }
  }
  return {};
}

template <typename Policy>
[[nodiscard]] LogExpected<void> Writer<Policy>::zero_copy_log_data(
  std::span<const std::byte> data,
  const MessageHandleType& message_handle,
  jewels::time::SteadyTime current_steady_time)
{
  if (!async_request_handle_.is_valid())
  {
    auto handle_result = async_write_request_pool_.make_shared_object();
    if (!handle_result)
    {
      std::pmr::string status_string{runtime_memory_resource_};
      fmt::format_to(
        std::back_inserter(status_string),
        "Failed to allocate an async write request handle: {}",
        handle_result.error());
      jewels::log_cerr_error("{}", status_string);
      return jewels::unexpected{set_writer_error(LogError::failed_to_allocate_async_request, std::move(status_string))};
    }
    async_request_handle_ = std::move(handle_result).value();
  }
  while (!data.empty())
  {
    const auto bytes_zero_copied = async_request_handle_->zero_copy_data(current_steady_time, data, message_handle);
    if (!guarded_state_->maybe_oldest_pending_data_timestamp.load(std::memory_order_relaxed).has_value())
    {
      guarded_state_->maybe_oldest_pending_data_timestamp.store(current_steady_time, std::memory_order_release);
    }
    data = data.last(data.size() - bytes_zero_copied);
    if (!data.empty())
    {
      if (const auto write_result = async_writer_.write_async(std::move(async_request_handle_)); !write_result)
      {
        guarded_state_->maybe_writer_error.store(LogError::async_write_error, std::memory_order_release);
        return write_result;
      }
      async_request_handle_ = {};
      guarded_state_->maybe_oldest_pending_data_timestamp.store(std::nullopt, std::memory_order_release);
      auto handle_result = async_write_request_pool_.make_shared_object();
      if (!handle_result)
      {
        std::pmr::string status_string{runtime_memory_resource_};
        fmt::format_to(
          std::back_inserter(status_string),
          "Failed to allocate an async write request handle: {}",
          handle_result.error());
        jewels::log_cerr_error("{}", status_string);
        return jewels::unexpected{
          set_writer_error(LogError::failed_to_allocate_async_request, std::move(status_string))};
      }
      async_request_handle_ = std::move(handle_result).value();
    }
  }
  return {};
}

template <typename Policy>
[[nodiscard]] LogExpected<void> Writer<Policy>::advance_to_next_buffer()
{
  if (async_request_handle_->is_full())
  {
    if (const auto write_result = async_writer_.write_async(std::move(async_request_handle_)); !write_result)
    {
      guarded_state_->maybe_writer_error.store(LogError::async_write_error, std::memory_order_release);
      return write_result;
    }
    async_request_handle_ = {};
    guarded_state_->maybe_oldest_pending_data_timestamp.store(std::nullopt, std::memory_order_release);
    auto handle_result = async_write_request_pool_.make_shared_object();
    if (!handle_result)
    {
      std::pmr::string status_string{runtime_memory_resource_};
      fmt::format_to(
        std::back_inserter(status_string),
        "Failed to allocate an async write request handle: {}",
        handle_result.error());
      jewels::log_cerr_error("{}", status_string);
      return jewels::unexpected{set_writer_error(LogError::failed_to_allocate_async_request, std::move(status_string))};
    }
    async_request_handle_ = std::move(handle_result).value();
  }
  auto get_buffer_result = write_buffer_pool_ptr_->get_shared_buffer();
  if (!get_buffer_result)
  {
    std::pmr::string status_string{runtime_memory_resource_};
    fmt::format_to(
      std::back_inserter(status_string), "Failed to allocate a write buffer: {}", get_buffer_result.error());
    jewels::log_cerr_error("{}", status_string);
    return jewels::unexpected{set_writer_error(LogError::failed_to_allocate_data_buffer, std::move(status_string))};
  }
  if (!async_request_handle_->add_buffer(std::move(get_buffer_result).value()))
  {
    std::pmr::string status_string{"Failed to add buffer to the async write request", runtime_memory_resource_};
    jewels::log_cerr_error("{}", status_string);
    return jewels::unexpected{set_writer_error(LogError::failed_to_add_buffer_to_request, std::move(status_string))};
  }
  return {};
}

template <typename Policy>
[[nodiscard]] LogExpected<void> Writer<Policy>::split_log_if_needed(
  LogTimestamp message_time, LogTimestamp log_time, jewels::time::SteadyTime current_steady_time)
{
  maybe_min_log_timestamp_ = std::min(log_time, maybe_min_log_timestamp_.value_or(log_time));
  maybe_max_log_timestamp_ = std::max(log_time, maybe_max_log_timestamp_.value_or(log_time));
  maybe_min_message_timestamp_ = std::min(message_time, maybe_min_message_timestamp_.value_or(message_time));
  maybe_max_message_timestamp_ = std::max(message_time, maybe_max_message_timestamp_.value_or(message_time));
  const auto offset_result = async_writer_.get_log_file_offset();
  if (!offset_result)
  {
    return jewels::unexpected(offset_result.error());
  }

  // The logic above guarantees that the timestamp optionals are all set.
  const auto over_max_log_duration =
    (max_log_file_duration_ == std::chrono::nanoseconds{0})
      ? false
      : maybe_max_log_timestamp_.value() - maybe_min_log_timestamp_.value() > max_log_file_duration_;
  if (
    ((*offset_result + (async_request_handle_.is_valid() ? async_request_handle_->get_write_size() : 0U)) >=
     max_log_file_size) ||
    over_max_log_duration)
  {
    if (const auto split_result = split_log(current_steady_time); !split_result)
    {
      return split_result;
    }
  }
  return {};
}

template <typename Policy>
[[nodiscard]] LogError Writer<Policy>::set_writer_error(LogError writer_error, std::string_view status_string)
{
  const std::lock_guard guard(guarded_state_->mutex);
  if (guarded_state_->status_string.empty())
  {
    guarded_state_->status_string = std::pmr::string{status_string, runtime_memory_resource_};
  }
  guarded_state_->maybe_writer_error.store(writer_error, std::memory_order_release);
  return writer_error;
}

template <typename Policy>
void Writer<Policy>::update_logging_rates(size_t record_size, size_t message_count)
{
  const std::lock_guard guard{guarded_state_->mutex};
  const auto current_steady_time = jewels::time::SteadyClock::now();
  guarded_state_->data_rate_filter.update(current_steady_time, record_size);
  guarded_state_->message_rate_filter.update(current_steady_time, message_count);
}

template <typename Policy>
[[nodiscard]] Writer<Policy>::PersistentChannelMessage Writer<Policy>::copy_persistent_channel_message(
  const MessageRecordHeader& record_header,
  std::span<const std::byte> header,
  std::span<const std::span<const std::byte>> data_spans)
{
  PersistentChannelMessage message_copy{};
  const auto data_size = std::accumulate(
    data_spans.begin(), data_spans.end(), size_t{0U}, [](size_t lhs, auto& rhs) { return lhs + rhs.size(); });
  const auto record_size = message_record_header_size + header.size() + data_size + sizeof(RecordTrailer);
  message_copy.record_header.header.record_type = RecordType::message;
  message_copy.record_header.header.record_size = static_cast<uint32_t>(record_size);
  message_copy.record_header.channel_id = record_header.channel_id;
  message_copy.record_header.sequence_number = record_header.sequence_number;
  message_copy.record_header.log_time_ns = record_header.log_time_ns;
  message_copy.record_header.message_time_ns = record_header.message_time_ns;
  message_copy.record_header.flags = record_header.flags;
  message_copy.record_header.flags.is_repeated = 1U;
  message_copy.record_header.header_length = record_header.header_length;
  message_copy.payload = std::pmr::vector<std::byte>(header.size() + data_size, runtime_memory_resource_);
  if (!header.empty())
  {
    std::memcpy(message_copy.payload.data(), header.data(), header.size());
  }
  auto data_offset = header.size();
  for (const auto span : data_spans)
  {
    if (!span.empty())
    {
      std::memcpy(&message_copy.payload.at(data_offset), span.data(), span.size());
      data_offset += span.size();
    }
  }
  std::array record_spans = {
    std::as_bytes(std::span{&message_copy.record_header, 1U}), std::as_bytes(std::span{message_copy.payload})};
  message_copy.record_trailer.xxh3_checksum = compute_xxh3_checksum(std::span{record_spans});
  return message_copy;
}

template <typename Policy>
[[nodiscard]] Writer<Policy>::PersistentChannelMessage Writer<Policy>::copy_persistent_channel_message(
  const MessageRecordHeader& record_header, std::span<const std::byte> header, std::span<const std::byte> data)
{
  return copy_persistent_channel_message(record_header, header, std::span{&data, 1U});
}

} // namespace clockwork_logging::onboard
