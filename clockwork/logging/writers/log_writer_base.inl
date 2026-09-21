// IWYU pragma: private, include "clockwork/logging/writers/log_writer_base.hh"
#pragma once

#include "clockwork/logging/writers/log_writer_base.hh"

#include "clockwork/logging/channel_type_clk_cc.hh"
#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/lite_compressor_interface.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/log_writer_config_clk_cc.hh"
#include "clockwork/logging/nolint_helper.hh"
#include "clockwork/logging/onboard/types.hh"
#include "clockwork/logging/onboard/writer.hh"
#include "clockwork/logging/onboard/writer_state.hh"
#include "clockwork/logging/writers/channel_message_rates_clk_cc.hh"
#include "clockwork/logging/writers/channel_message_rates_config_clk_cc.hh"
#include "clockwork/logging/writers/log_writer_state_clk_cc.hh"
#include "clockwork/logging/writers/message_rate_counter.hh"
#include "clockwork/logging/writers/rate_status_clk_cc.hh"
#include "clockwork/pinion/abstract_channel.hh"
#include "clockwork/pinion/abstract_channel_factory.hh"
#include "clockwork/pinion/buffer_layout.hh"
#include "clockwork/pinion/channel_config_clk_cc.hh"
#include "clockwork/pinion/channel_observer.hh"
#include "clockwork/pinion/meta_channel_factory.hh"
#include "clockwork/pinion/slot_ref.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/runners/epoll_manager.hh"
#include "clockwork/scaffolding/channels.hh"
#include "jewels/container/compare.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <fmt/base.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <compare>
#include <cstddef>
#include <functional>
#include <iterator>
#include <list>
#include <map>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace clockwork_logging
{

template <typename Derived, typename BufferPoolT>
LogWriterBase<Derived, BufferPoolT>::GuardedState::GuardedState(
  jewels::memory::MemoryResource memory_resource,
  const clockwork::Tappy<LogWriterConfig<>>& log_writer_config,
  const clockwork::Tappy<ChannelMessageRatesConfig>& channel_rates_config,
  std::pmr::unordered_set<std::pmr::string>& channel_name_strings)
  : message_counts(memory_resource),
    logged_persistent_channels(memory_resource),
    message_rate_counter(memory_resource, channel_rates_config)
{
  const auto current_steady_time = jewels::time::SteadyClock::now();
  for (const auto& channel_config : log_writer_config.get_channels())
  {
    const auto channel_name_iter =
      // NOLINTNEXTLINE(modernize-use-emplace) Compiler doesn't accept emplace(channel_name, memory_resource_)
      channel_name_strings.emplace(std::pmr::string{channel_config.get_channel_name(), memory_resource}).first;
    message_rate_counter.add_channel(*channel_name_iter, current_steady_time);
  }
}

template <typename Derived, typename BufferPoolT>
LogWriterBase<Derived, BufferPoolT>::LogWriterBase(
  jewels::memory::MemoryResource memory_resource,
  const clockwork::Tappy<LogWriterConfig<>>& log_writer_config,
  std::string_view pinion_shm_root,
  std::string_view pinion_namespace,
  size_t buffer_pool_size,
  std::chrono::nanoseconds max_log_file_duration,
  const clockwork::Tappy<ChannelMessageRatesConfig>& channel_rates_config)
  : memory_resource_(std::move(memory_resource)),
    channel_name_strings_(memory_resource_),
    guarded_state_(
      jewels::memory::make_pmr_unique<GuardedState>(
        memory_resource_, memory_resource_, log_writer_config, channel_rates_config, channel_name_strings_)),
    pinion_shm_root_(pinion_shm_root, memory_resource_),
    pinion_namespace_(pinion_namespace, memory_resource_),
    subscription_configs_(memory_resource_),
    channel_observer_ptrs_(log_writer_config.get_channels().size(), memory_resource_),
    subscriber_ptrs_(log_writer_config.get_channels().size(), memory_resource_),
    pending_subscriptions_(memory_resource_),
    persistent_channels_(memory_resource_),
    buffer_pool_ptr_(
      jewels::memory::allocate_shared<BufferPoolType, std::pmr::polymorphic_allocator<BufferPoolType>>(
        memory_resource_, memory_resource_, buffer_pool_size)),
    writer_(
      memory_resource_, memory_resource_, buffer_pool_ptr_, max_log_file_duration, onboard::WriterEnvironment::normal),
    epoll_(memory_resource_)
{
  subscription_configs_.reserve(static_cast<size_t>(log_writer_config.get_channels().size()));
  // Load the channel message rates to initialize the counters
  std::ignore = get_channel_message_rates();
}

template <typename Derived, typename BufferPoolT>
LogWriterBase<Derived, BufferPoolT>::~LogWriterBase()
{
  const auto state = get_state();
  if ((state == LogWriterState::logging) || (state == LogWriterState::degraded))
  {
    if (const auto stop_result = stop_logging(); !stop_result)
    {
      jewels::log_cerr_error("Failed to stop logging in destructor: {}", stop_result.error());
    }
  }
  if (is_initialized_)
  {
    if (const auto drain_result = writer_.drain_async_operations(); !drain_result)
    {
      jewels::log_cerr_error("Failed to drain outstanding async operations: {}", drain_result.error());
    }
  }
}

template <typename Derived, typename BufferPoolT>
[[nodiscard]] LogExpected<void> LogWriterBase<Derived, BufferPoolT>::start_logging(const std::string_view log_path)
{
  if (const auto start_result = start_logging_paused(log_path); !start_result)
  {
    return jewels::unexpected(start_result.error());
  }
  return resume_logging();
}

template <typename Derived, typename BufferPoolT>
[[nodiscard]] LogExpected<void>
LogWriterBase<Derived, BufferPoolT>::start_logging_paused(const std::string_view log_path)
{
  if (!is_initialized_)
  {
    return jewels::unexpected(LogError::not_initialized);
  }
  const auto state = get_state();
  if (state != LogWriterState::stopped)
  {
    return state == LogWriterState::failed ? jewels::unexpected(LogError::failed)
                                           : jewels::unexpected(LogError::already_open);
  }
  if (const auto open_result = writer_.open_log_paused(log_path, log_file_prefix); !open_result)
  {
    auto status_string = std::pmr::string{memory_resource_};
    fmt::format_to(std::back_inserter(status_string), "Failed to open log file: {}", open_result.error());
    jewels::log_cerr_error("{}", status_string);
    set_state_to_failed(std::move(status_string));
    return open_result;
  }
  guarded_state_->state.store(LogWriterState::paused, std::memory_order_release);
  return {};
}

template <typename Derived, typename BufferPoolT>
[[nodiscard]] LogExpected<void> LogWriterBase<Derived, BufferPoolT>::pause_logging()
{
  const auto state = get_state();
  if (state == LogWriterState::failed)
  {
    return jewels::unexpected(LogError::failed);
  }
  if (state != LogWriterState::logging && state != LogWriterState::degraded)
  {
    return jewels::unexpected(LogError::not_open);
  }
  if (const auto pause_result = writer_.pause_logging(jewels::time::SteadyClock::now()); !pause_result)
  {
    jewels::log_cerr_error("Failed to pause the writer: {}", pause_result.error());
  }
  guarded_state_->state.store(LogWriterState::paused, std::memory_order_release);
  return {};
}

template <typename Derived, typename BufferPoolT>
[[nodiscard]] LogExpected<void> LogWriterBase<Derived, BufferPoolT>::resume_logging()
{
  const auto state = get_state();
  if (state != LogWriterState::paused)
  {
    return state == LogWriterState::failed ? jewels::unexpected(LogError::failed)
                                           : jewels::unexpected(LogError::not_paused);
  }
  const auto current_steady_time = jewels::time::SteadyClock::now();
  if (const auto resume_result = writer_.resume_logging(current_steady_time); !resume_result)
  {
    std::pmr::string status_string{memory_resource_};
    fmt::format_to(std::back_inserter(status_string), "Failed to open log file: {}", resume_result.error());
    jewels::log_cerr_error("{}", status_string);
    set_state_to_failed(std::move(status_string));
    return resume_result;
  }
  guarded_state_->state.store(LogWriterState::logging, std::memory_order_release);
  return {};
}

template <typename Derived, typename BufferPoolT>
[[nodiscard]] LogExpected<void> LogWriterBase<Derived, BufferPoolT>::stop_logging()
{
  const auto state = get_state();
  if (state == LogWriterState::failed)
  {
    return jewels::unexpected(LogError::failed);
  }
  if (state == LogWriterState::stopped)
  {
    return jewels::unexpected(LogError::not_open);
  }
  if (const auto close_result = writer_.close_log(jewels::time::SteadyClock::now()); !close_result)
  {
    jewels::log_cerr_error("Failed to close the writer: {}", close_result.error());
  }
  if (const auto drain_result = writer_.drain_async_operations(); !drain_result)
  {
    jewels::log_cerr_error("Failed to drain outstanding async operations: {}", drain_result.error());
  }
  guarded_state_->state.store(LogWriterState::stopped, std::memory_order_release);
  return {};
}

template <typename Derived, typename BufferPoolT>
[[nodiscard]] LogExpected<void> LogWriterBase<Derived, BufferPoolT>::drain_async_operations()
{
  if (const auto drain_result = writer_.drain_async_operations(); !drain_result)
  {
    return jewels::unexpected(drain_result.error());
  }
  update_max_write_backlog(jewels::time::SteadyClock::now());
  return {};
}

template <typename Derived, typename BufferPoolT>
void LogWriterBase<Derived, BufferPoolT>::run_for(std::chrono::nanoseconds duration)
{
  if (!is_initialized_)
  {
    std::this_thread::sleep_for(duration);
    return;
  }
  if (get_state() == LogWriterState::failed)
  {
    std::this_thread::sleep_for(duration);
    return;
  }
  auto now = jewels::time::SteadyClock::now();
  writer_.periodic_callback(now);
  last_writer_periodic_callback_time_ = now;
  if (now - last_pending_subscription_poll_time_ >= poll_pending_subscriptions_interval)
  {
    poll_pending_subscriptions();
    last_pending_subscription_poll_time_ = now;
  }
  now = jewels::time::SteadyClock::now();
  const auto end_time = now + duration;
  while (now < end_time)
  {
    const auto timeout = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - now);
    poll_subscriptions(timeout);
    now = jewels::time::SteadyClock::now();
  }
  update_max_write_backlog(now);
}

template <typename Derived, typename BufferPoolT>
void LogWriterBase<Derived, BufferPoolT>::message_callback(
  jewels::time::SyncTime /*current_time*/,
  std::string_view channel_name,
  const ::clockwork::pinion::SlotRef& message_ref)
{
  const auto now = jewels::time::SteadyClock::now();
  {
    const std::lock_guard guard{guarded_state_->mutex};
    guarded_state_->message_rate_counter.update_channel(channel_name, now);
  }
  if (last_writer_periodic_callback_time_ + writer_periodic_callback_interval < now)
  {
    writer_.periodic_callback(now);
    update_max_write_backlog(now);
    last_writer_periodic_callback_time_ = now;
  }
  static_cast<Derived*>(this)->message_handler(channel_name, message_ref);
}

template <typename Derived, typename BufferPoolT>
void LogWriterBase<Derived, BufferPoolT>::drop_callback(std::string_view channel_name, size_t drop_count)
{
  if (guarded_state_->drop_count.fetch_add(drop_count, std::memory_order_release) == 0)
  {
    jewels::log_cerr_error("Dropped {} messages on {}", drop_count, channel_name);
  }
}

template <typename Derived, typename BufferPoolT>
[[nodiscard]] LogWriterState LogWriterBase<Derived, BufferPoolT>::get_state()
{
  const auto writer_state = writer_.get_state();
  if (writer_state == onboard::WriterState::failed)
  {
    return LogWriterState::failed;
  }
  const auto state = guarded_state_->state.load(std::memory_order_acquire);
  if (state == LogWriterState::logging)
  {
    if (guarded_state_->is_degraded.load(std::memory_order_acquire) || writer_state == onboard::WriterState::degraded)
    {
      return LogWriterState::degraded;
    }
  }
  return state;
}

template <typename Derived, typename BufferPoolT>
[[nodiscard]] std::pmr::string LogWriterBase<Derived, BufferPoolT>::get_status_string()
{
  const std::lock_guard guard(guarded_state_->mutex);
  return guarded_state_->status_string;
}

template <typename Derived, typename BufferPoolT>
[[nodiscard]] onboard::WriterStatusResult LogWriterBase<Derived, BufferPoolT>::get_status()
{
  auto writer_status = writer_.get_status();
  if (!writer_status.status_string.empty())
  {
    return writer_status;
  }
  const std::lock_guard guard(guarded_state_->mutex);
  writer_status.status_string = guarded_state_->status_string;
  return writer_status;
}

template <typename Derived, typename BufferPoolT>
[[nodiscard]] size_t LogWriterBase<Derived, BufferPoolT>::get_and_reset_drop_count()
{
  return guarded_state_->drop_count.exchange(0U, std::memory_order_release) + writer_.get_and_reset_drop_count();
}

template <typename Derived, typename BufferPoolT>
[[nodiscard]] std::chrono::nanoseconds LogWriterBase<Derived, BufferPoolT>::get_and_reset_max_write_backlog() noexcept
{
  return guarded_state_->max_write_backlog.exchange(std::chrono::nanoseconds(0), std::memory_order_release);
}

template <typename Derived, typename BufferPoolT>
bool LogWriterBase<Derived, BufferPoolT>::get_is_degraded()
{
  return guarded_state_->is_degraded.load(std::memory_order_acquire);
}

template <typename Derived, typename BufferPoolT>
[[nodiscard]] LogExpected<void>
LogWriterBase<Derived, BufferPoolT>::initialize(const clockwork::Tappy<LogWriterConfig<>>& log_writer_config)
{
  if (clockwork::Tappy<ChannelMessageRates<>>::max_num_channels < log_writer_config.get_channels().size())
  {
    std::pmr::string error_string{memory_resource_};
    fmt::format_to(
      std::back_inserter(error_string),
      "Number of logged channels ({}) exceeds max supported by ChannelMessageRates ({})",
      log_writer_config.get_channels().size(),
      clockwork::Tappy<ChannelMessageRates<>>::max_num_channels);
    throw std::invalid_argument(error_string.c_str());
  }
  if (is_initialized_)
  {
    return jewels::unexpected(LogError::already_initialized);
  }
  guarded_state_->num_pending_subscriptions.store(log_writer_config.get_channels().size(), std::memory_order_release);
  for (const auto& channel_config : log_writer_config.get_channels())
  {
    const auto channel_name_iter =
      // NOLINTNEXTLINE(modernize-use-emplace) Compiler doesn't accept emplace(channel_name, memory_resource_)
      channel_name_strings_.emplace(std::pmr::string{channel_config.get_channel_name(), memory_resource_}).first;
    subscription_configs_.emplace_back(
      *channel_name_iter,
      channel_config.get_uuid().to_string(memory_resource_),
      channel_config.get_num_slots(),
      channel_config.get_message_size(),
      channel_config.get_channel_type());
    pending_subscriptions_.emplace_back(subscription_configs_.size() - 1U);
  }
  // TODO(OI-4714): Plumb channel types and publisher/subscriber keys through the log writer config
  std::pmr::unordered_map<std::pmr::string, clockwork::pinion::ChannelType> channel_types;
  std::pmr::unordered_map<std::pmr::string, std::pmr::vector<std::pmr::string>> publisher_keys;
  std::pmr::unordered_map<std::pmr::string, std::pmr::string> subscriber_keys;
  auto channel_factory_result = clockwork::pinion::MetaChannelFactory::make(
    memory_resource_,
    std::move(channel_types),
    std::move(publisher_keys),
    std::move(subscriber_keys),
    pinion_namespace_,
    pinion_shm_root_);
  if (!channel_factory_result)
  {
    std::pmr::string status_string{"Failed to make shared memory channel factory", memory_resource_};
    jewels::log_cerr_error("{}", status_string);
    set_state_to_failed(std::move(status_string));
    return jewels::unexpected(LogError::clockwork_error);
  }
  channel_factory_ptr_ = std::allocate_shared<
    clockwork::pinion::MetaChannelFactory,
    std::pmr::polymorphic_allocator<clockwork::pinion::MetaChannelFactory>>(
    memory_resource_, std::move(channel_factory_result).value());
  if (const auto channel_result = add_channels(log_writer_config); !channel_result)
  {
    return jewels::unexpected(channel_result.error());
  }
  is_initialized_ = true;
  return {};
}

template <typename Derived, typename BufferPoolT>
[[nodiscard]] std::pmr::map<std::string_view, typename LogWriterBase<Derived, BufferPoolT>::ChannelMessageRateMapEntry>
LogWriterBase<Derived, BufferPoolT>::get_channel_message_rates()
{
  const std::lock_guard guard{guarded_state_->mutex};
  std::pmr::map<std::string_view, ChannelMessageRateMapEntry> rate_map{memory_resource_};
  const auto current_steady_time = jewels::time::SteadyClock::now();
  num_low_rate_channels_ = 0U;
  low_rate_channel_name_.clear();
  const auto rates_are_valid = guarded_state_->message_rate_counter.is_warmed_up();
  for (auto& [channel_name, entry] : guarded_state_->message_rate_counter.get_channel_rate_map())
  {
    const auto msg_rate_hz = entry.rate_filter.get_rate(current_steady_time);
    const auto rate_status =
      (guarded_state_->logged_persistent_channels.contains(channel_name) || msg_rate_hz >= entry.min_msg_rate_hz)
        ? RateStatus::good
        : RateStatus::low;
    if (rates_are_valid && rate_status == RateStatus::low)
    {
      ++num_low_rate_channels_;
      if (low_rate_channel_name_.empty())
      {
        low_rate_channel_name_ = std::pmr::string{channel_name, memory_resource_};
      }
    }
    rate_map.emplace(
      channel_name,
      ChannelMessageRateMapEntry{
        .msg_rate_hz = msg_rate_hz,
        .rate_status = rate_status,
      });
  }
  return rate_map;
}

template <typename Derived, typename BufferPoolT>
[[nodiscard]] size_t LogWriterBase<Derived, BufferPoolT>::get_num_low_rate_channels() const
{
  return num_low_rate_channels_;
}

template <typename Derived, typename BufferPoolT>
[[nodiscard]] std::string_view LogWriterBase<Derived, BufferPoolT>::get_low_rate_channel_name() const
{
  return low_rate_channel_name_;
}

template <typename Derived, typename BufferPoolT>
[[nodiscard]] jewels::memory::NonNullSharedPtr<LiteCompressorInterface>
LogWriterBase<Derived, BufferPoolT>::get_channel_compressor(std::string_view channel_name) const
{
  return writer_.get_channel_compressor(channel_name);
}

template <typename Derived, typename BufferPoolT>
[[nodiscard]] size_t LogWriterBase<Derived, BufferPoolT>::get_num_pending_subscriptions() const
{
  return guarded_state_->num_pending_subscriptions.load(std::memory_order_acquire);
}

template <typename Derived, typename BufferPoolT>
[[nodiscard]] std::pmr::unordered_map<std::pmr::string, size_t>
LogWriterBase<Derived, BufferPoolT>::get_message_counts() const
{
  const std::lock_guard guard(guarded_state_->mutex);
  std::pmr::unordered_map<std::pmr::string, size_t> message_counts{memory_resource_};
  for (const auto [channel_name, count] : guarded_state_->message_counts)
  {
    message_counts.emplace(std::pmr::string{channel_name, memory_resource_}, count);
  }
  return message_counts;
}

template <typename Derived, typename BufferPoolT>
void LogWriterBase<Derived, BufferPoolT>::clear_message_counts()
{
  const std::lock_guard guard(guarded_state_->mutex);
  guarded_state_->message_counts.clear();
}

template <typename Derived, typename BufferPoolT>
[[nodiscard]] LogExpected<void>
LogWriterBase<Derived, BufferPoolT>::add_channels(const clockwork::Tappy<LogWriterConfig<>>& log_writer_config)
{
  for (const auto& channel : log_writer_config.get_channels())
  {
    const auto channel_name_iter =
      // NOLINTNEXTLINE(modernize-use-emplace) Compiler doesn't accept emplace(channel_name, memory_resource_)
      channel_name_strings_.emplace(std::pmr::string{channel.get_channel_name(), memory_resource_}).first;
    if (channel.get_channel_type() == ChannelType::persistent)
    {
      persistent_channels_.emplace(*channel_name_iter);
    }
    if (const auto channel_result = writer_.add_channel(
          onboard::LoggedChannelMetadata{
            .channel_name = *channel_name_iter,
            .compression_type = CompressionType::none,
            .message_encoding = channel.get_message_encoding(),
            .channel_type = channel.get_channel_type(),
            .schema_name = channel.get_schema_name(),
            .schema_encoding = channel.get_schema_encoding(),
            .schema_definition = nolint_helper::byte_span_to_string_view(channel.get_schema_definition()),
          },
          jewels::time::SteadyClock::now());
        !channel_result)
    {
      std::pmr::string status_string{memory_resource_};
      fmt::format_to(
        std::back_inserter(status_string),
        "Failed to add channel {}' : {}",
        channel.get_channel_name(),
        channel_result.error());
      jewels::log_cerr_error("{}", status_string);
      set_state_to_failed(std::move(status_string));
      return channel_result;
    }
  }
  return {};
}

template <typename Derived, typename BufferPoolT>
void LogWriterBase<Derived, BufferPoolT>::poll_pending_subscriptions()
{
  if (pending_subscriptions_.empty())
  {
    return;
  }
  jewels::log_cerr_info("Polling subscriptions: {} remaining", pending_subscriptions_.size());
  auto pending_subscription_iter = pending_subscriptions_.begin();
  while (pending_subscription_iter != pending_subscriptions_.end())
  {
    const auto subscription_index = *pending_subscription_iter;
    const auto& pending_subscription = subscription_configs_.at(subscription_index);
    auto open_result = channel_factory_ptr_->open_subscriber(
      pending_subscription.uuid_str,
      pending_subscription.channel_name,
      clockwork::pinion::BufferLayout{
        .num_slots = pending_subscription.num_slots,
        .message_size = pending_subscription.message_size_b,
        .is_published_once = false,
      },
      1U);
    if (open_result == jewels::unexpected(clockwork::pinion::AbstractChannel::Error::missing))
    {
      ++pending_subscription_iter;
      continue;
    }
    if (!open_result)
    {
      std::pmr::string status_string{memory_resource_};
      fmt::format_to(
        std::back_inserter(status_string),
        "Failed to create subscription for {}: {}",
        pending_subscription.channel_name,
        open_result.error());
      jewels::log_cerr_error("{}", status_string);
      set_is_degraded(std::move(status_string));
      pending_subscription_iter = pending_subscriptions_.erase(pending_subscription_iter);
      continue;
    }
    subscriber_ptrs_.at(subscription_index) = std::move(open_result).value();
    const auto& subscriber_ptr = subscriber_ptrs_.at(subscription_index);
    channel_observer_ptrs_.at(subscription_index) = std::allocate_shared<
      ::clockwork::pinion::ChannelObserver,
      std::pmr::polymorphic_allocator<::clockwork::pinion::ChannelObserver>>(
      memory_resource_,
      memory_resource_,
      subscriber_ptr,
      jewels::memory::make_non_null_from_ref(*this),
      pending_subscription.channel_name,
      pending_subscription.channel_type);
    auto& observer = *channel_observer_ptrs_.at(subscription_index);
    if (!subscriber_ptr->add_observer(jewels::memory::make_non_null_from_ref(observer)))
    {
      std::pmr::string status_string{memory_resource_};
      fmt::format_to(
        std::back_inserter(status_string), "Failed to add observer for {}", pending_subscription.channel_name);
      jewels::log_cerr_error("{}", status_string);
      set_is_degraded(std::move(status_string));
      subscriber_ptrs_.at(subscription_index).reset();
      channel_observer_ptrs_.at(subscription_index).reset();
      pending_subscription_iter = pending_subscriptions_.erase(pending_subscription_iter);
      continue;
    }
    clockwork::scaffolding::bind_channel_to_epoll(subscriber_ptr, epoll_);
    jewels::log_cerr_info("Subscribed to {} : {}", pending_subscription.channel_name, pending_subscription.uuid_str);
    observer.notify({});
    pending_subscription_iter = pending_subscriptions_.erase(pending_subscription_iter);
  }
  guarded_state_->num_pending_subscriptions.store(pending_subscriptions_.size(), std::memory_order_release);
}

template <typename Derived, typename BufferPoolT>
void LogWriterBase<Derived, BufferPoolT>::poll_subscriptions(std::chrono::milliseconds timeout)
{
  if (auto wait_result = epoll_.wait(timeout); !wait_result && wait_result.error().value() != EINTR)
  {
    jewels::log_cerr_error("Epoll wait failed: {}", wait_result.error());
  }
}

template <typename Derived, typename BufferPoolT>
[[nodiscard]] LogExpected<void> LogWriterBase<Derived, BufferPoolT>::log_message(
  std::string_view channel_name,
  const ::clockwork::pinion::SlotRef& message_handle,
  LogTimestamp log_time,
  jewels::time::SteadyTime current_steady_time)
{
  const auto state = get_state();
  if (state == LogWriterState::failed)
  {
    return jewels::unexpected(LogError::failed);
  }
  if (state != LogWriterState::logging && state != LogWriterState::degraded)
  {
    return jewels::unexpected(LogError::not_open);
  }
  {
    const std::lock_guard guard(guarded_state_->mutex);
    ++guarded_state_->message_counts[channel_name];
    if (is_persistent_channel(channel_name))
    {
      guarded_state_->logged_persistent_channels.emplace(channel_name);
    }
  }
  const auto write_result = writer_.log_clockwork_message(channel_name, message_handle, log_time, current_steady_time);
  if (!write_result && write_result != jewels::unexpected(LogError::message_dropped))
  {
    std::pmr::string status_string{memory_resource_};
    fmt::format_to(
      std::back_inserter(status_string), "Fatal error writing to '{}' : {}", channel_name, write_result.error());
    jewels::log_cerr_error("{}", status_string);
    set_state_to_failed(std::move(status_string));
  }
  return write_result;
}

template <typename Derived, typename BufferPoolT>
[[nodiscard]] LogExpected<void> LogWriterBase<Derived, BufferPoolT>::log_message_wait(
  std::string_view channel_name,
  const ::clockwork::pinion::SlotRef& message_handle,
  LogTimestamp log_time,
  jewels::time::SteadyTime current_steady_time)
{
  const auto state = get_state();
  if (state == LogWriterState::failed)
  {
    return jewels::unexpected(LogError::failed);
  }
  if (state != LogWriterState::logging && state != LogWriterState::degraded)
  {
    return jewels::unexpected(LogError::not_open);
  }
  {
    const std::lock_guard guard(guarded_state_->mutex);
    ++guarded_state_->message_counts[channel_name];
    if (is_persistent_channel(channel_name))
    {
      guarded_state_->logged_persistent_channels.emplace(channel_name);
    }
  }
  const auto write_result =
    writer_.log_clockwork_message_wait(channel_name, message_handle, log_time, current_steady_time);
  if (!write_result && write_result != jewels::unexpected(LogError::message_dropped))
  {
    std::pmr::string status_string{memory_resource_};
    fmt::format_to(
      std::back_inserter(status_string), "Fatal error writing to '{}' : {}", channel_name, write_result.error());
    jewels::log_cerr_error("{}", status_string);
    set_state_to_failed(std::move(status_string));
  }
  return write_result;
}

template <typename Derived, typename BufferPoolT>
[[nodiscard]] LogExpected<void> LogWriterBase<Derived, BufferPoolT>::save_persistent_message(
  std::string_view channel_name, const ::clockwork::pinion::SlotRef& message_handle, LogTimestamp log_time)
{
  if (!is_persistent_channel(channel_name))
  {
    jewels::log_cerr_error("Trying to save message on non-persistent channel {}", channel_name);
    return jewels::unexpected(LogError::not_persistent);
  }
  {
    const std::lock_guard guard(guarded_state_->mutex);
    if (is_persistent_channel(channel_name))
    {
      guarded_state_->logged_persistent_channels.emplace(channel_name);
    }
  }
  const auto save_result = writer_.save_persistent_clockwork_message(channel_name, message_handle, log_time);
  if (!save_result && save_result != jewels::unexpected(LogError::message_dropped))
  {
    std::pmr::string status_string{memory_resource_};
    fmt::format_to(
      std::back_inserter(status_string), "Fatal error writing to '{}' : {}", channel_name, save_result.error());
    jewels::log_cerr_error("{}", status_string);
    set_state_to_failed(std::move(status_string));
    return jewels::unexpected(save_result.error());
  }
  return {};
}

template <typename Derived, typename BufferPoolT>
[[nodiscard]] LogExpected<void> LogWriterBase<Derived, BufferPoolT>::log_message(
  const onboard::ZeroCopyMessage& message, bool is_lite_compressed, jewels::time::SteadyTime current_steady_time)
{
  const auto state = get_state();
  if (state == LogWriterState::failed)
  {
    return jewels::unexpected(LogError::failed);
  }
  if (state != LogWriterState::logging && state != LogWriterState::degraded)
  {
    return jewels::unexpected(LogError::not_open);
  }
  {
    const std::lock_guard guard(guarded_state_->mutex);
    ++guarded_state_->message_counts[message.channel_name];
    if (is_persistent_channel(message.channel_name))
    {
      guarded_state_->logged_persistent_channels.emplace(message.channel_name);
    }
  }
  const auto write_result = writer_.log_message(message, is_lite_compressed, current_steady_time);
  if (!write_result && write_result != jewels::unexpected(LogError::message_dropped))
  {
    std::pmr::string status_string{memory_resource_};
    fmt::format_to(
      std::back_inserter(status_string),
      "Fatal error writing to '{}' : {}",
      message.channel_name,
      write_result.error());
    jewels::log_cerr_error("{}", status_string);
    set_state_to_failed(std::move(status_string));
  }
  return write_result;
}

template <typename Derived, typename BufferPoolT>
[[nodiscard]] LogExpected<void> LogWriterBase<Derived, BufferPoolT>::log_message_wait(
  const onboard::ZeroCopyMessage& message, bool is_lite_compressed, jewels::time::SteadyTime current_steady_time)
{
  const auto state = get_state();
  if (state == LogWriterState::failed)
  {
    return jewels::unexpected(LogError::failed);
  }
  if (state != LogWriterState::logging && state != LogWriterState::degraded)
  {
    return jewels::unexpected(LogError::not_open);
  }
  {
    const std::lock_guard guard(guarded_state_->mutex);
    ++guarded_state_->message_counts[message.channel_name];
    if (is_persistent_channel(message.channel_name))
    {
      guarded_state_->logged_persistent_channels.emplace(message.channel_name);
    }
  }
  const auto write_result = writer_.log_message_wait(message, is_lite_compressed, current_steady_time);
  if (!write_result && write_result != jewels::unexpected(LogError::message_dropped))
  {
    std::pmr::string status_string{memory_resource_};
    fmt::format_to(
      std::back_inserter(status_string),
      "Fatal error writing to '{}' : {}",
      message.channel_name,
      write_result.error());
    jewels::log_cerr_error("{}", status_string);
    set_state_to_failed(std::move(status_string));
  }
  return write_result;
}

template <typename Derived, typename BufferPoolT>
[[nodiscard]] LogExpected<void> LogWriterBase<Derived, BufferPoolT>::save_persistent_message(
  const onboard::ZeroCopyMessage& message, bool is_lite_compressed)
{
  if (!is_persistent_channel(message.channel_name))
  {
    jewels::log_cerr_error("Trying to save message on non-persistent channel {}", message.channel_name);
    return jewels::unexpected(LogError::not_persistent);
  }
  {
    const std::lock_guard guard(guarded_state_->mutex);
    if (is_persistent_channel(message.channel_name))
    {
      guarded_state_->logged_persistent_channels.emplace(message.channel_name);
    }
  }
  const auto save_result = writer_.save_persistent_message(message, is_lite_compressed);
  if (!save_result && save_result != jewels::unexpected(LogError::message_dropped))
  {
    std::pmr::string status_string{memory_resource_};
    fmt::format_to(
      std::back_inserter(status_string), "Fatal error writing to '{}' : {}", message.channel_name, save_result.error());
    jewels::log_cerr_error("{}", status_string);
    set_state_to_failed(std::move(status_string));
    return jewels::unexpected(save_result.error());
  }
  return {};
}

template <typename Derived, typename BufferPoolT>
void LogWriterBase<Derived, BufferPoolT>::update_max_write_backlog(jewels::time::SteadyTime current_steady_time)
{
  while (true)
  {
    const auto backlog_result = writer_.get_write_backlog(current_steady_time);
    if (!backlog_result)
    {
      break;
    }
    auto current_max_backlog = guarded_state_->max_write_backlog.load(std::memory_order_acquire);
    auto new_max_backlog = std::max(current_max_backlog, backlog_result.value());
    if (new_max_backlog == current_max_backlog)
    {
      break;
    }
    if (guarded_state_->max_write_backlog.compare_exchange_weak(
          current_max_backlog, new_max_backlog, std::memory_order_release, std::memory_order_relaxed))
    {
      break;
    }
  }
}

template <typename Derived, typename BufferPoolT>
void LogWriterBase<Derived, BufferPoolT>::set_state_to_failed(std::string_view status_string)
{
  const std::lock_guard guard(guarded_state_->mutex);
  if (guarded_state_->status_string.empty())
  {
    guarded_state_->status_string = std::pmr::string{status_string, memory_resource_};
  }
  guarded_state_->state.store(LogWriterState::failed, std::memory_order_release);
}

template <typename Derived, typename BufferPoolT>
void LogWriterBase<Derived, BufferPoolT>::set_is_degraded(std::string_view status_string)
{
  const std::lock_guard guard(guarded_state_->mutex);
  if (guarded_state_->status_string.empty())
  {
    guarded_state_->status_string = std::pmr::string{status_string, memory_resource_};
  }
  guarded_state_->is_degraded.store(true, std::memory_order_release);
}

template <typename Derived, typename BufferPoolT>
[[nodiscard]] bool LogWriterBase<Derived, BufferPoolT>::is_persistent_channel(std::string_view channel_name) const
{
  return persistent_channels_.contains(channel_name);
}

template <typename Derived, typename BufferPoolT>
[[nodiscard]] const jewels::memory::NonNullSharedPtr<typename LogWriterBase<Derived, BufferPoolT>::BufferPoolType>&
LogWriterBase<Derived, BufferPoolT>::buffer_pool_ptr() const
{
  return buffer_pool_ptr_;
}

} // namespace clockwork_logging
