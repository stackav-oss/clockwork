// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/channel_type_clk_cc.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/log_writer_config_clk_cc.hh"
#include "clockwork/logging/onboard/clockwork_writer_policy.hh"
#include "clockwork/logging/onboard/types.hh"
#include "clockwork/logging/onboard/writer.hh"
#include "clockwork/logging/writers/channel_message_rates_config_clk_cc.hh"
#include "clockwork/logging/writers/log_writer_state_clk_cc.hh"
#include "clockwork/logging/writers/message_rate_counter.hh"
#include "clockwork/logging/writers/rate_status_clk_cc.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/channel_observer.hh"
#include "clockwork/pinion/channel_observer_client.hh"
#include "clockwork/pinion/shm_channel_factory.hh"
#include "clockwork/pinion/shm_subscriber.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/runners/epoll_manager.hh"
#include "jewels/math/constants.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/shared_pool/shared_buffer_pool.hh"
#include "jewels/time/sync_time.hh"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace clockwork_logging
{

namespace detail
{

/// Default log writer buffer size
constexpr size_t default_log_writer_buffer_size = 2U * jewels::math::constants::bytes_per_mib<size_t>;

/// Default log writer buffer alignment
constexpr size_t default_log_writer_buffer_alignment = 2048U;

} // namespace detail

/// Base class for writers that subscribe to channels and write messages to an onboard format log
///
/// This class is designed to run in a multithreaded environment where one thread
/// is writing messages to the log and another thread is reporting the status.
/// Unless otherwise indicated, methods on this class *SHALL ONLY* be called
/// from the thread doing the writing.
///
/// @tparam Derived Derived writer type
/// @tparam BufferPoolT Buffer pool type
template <
  typename Derived,
  typename BufferPoolT =
    jewels::SharedBufferPool<detail::default_log_writer_buffer_size, detail::default_log_writer_buffer_alignment>>
class LogWriterBase : public clockwork::pinion::ChannelObserverClient
{
public:
  /// Buffer pool type
  using BufferPoolType = BufferPoolT;

  /// Onboard writer policy
  using OnboardWriterPolicy = onboard::ClockworkWriterPolicy<BufferPoolType>;

private:
  /// Struct used to store state shared between the writer thread and the thread
  /// that reports the writer status
  struct GuardedState
  {
    /// Constructor
    /// @param[in] memory_resource Memory resource
    /// @param[in] log_writer_config Log writer configuration
    /// @param[in] channel_rates_config Channel message rates config
    /// @param[in,out] channel_name_strings Backing storage for channel name string views
    GuardedState(
      jewels::memory::MemoryResource memory_resource,
      const clockwork::Tappy<LogWriterConfig<>>& log_writer_config,
      const clockwork::Tappy<ChannelMessageRatesConfig>& channel_rates_config,
      std::pmr::unordered_set<std::pmr::string>& channel_name_strings);

    ~GuardedState() noexcept = default;

    GuardedState(const GuardedState&) = delete;
    GuardedState& operator=(const GuardedState&) = delete;
    GuardedState(GuardedState&&) = delete;
    GuardedState& operator=(GuardedState&&) = delete;

    /// Writer state
    std::atomic<LogWriterState> state{LogWriterState::stopped};

    /// Dropped message count
    std::atomic<size_t> drop_count{0U};

    /// Flag set when the writer is degraded because it failed to subscribe to a logged channel
    std::atomic<bool> is_degraded{false};

    /// Maximum write backlog
    std::atomic<std::chrono::nanoseconds> max_write_backlog{std::chrono::nanoseconds(0)};

    /// Mutex guarding the status string
    mutable std::mutex mutex;

    /// Human readable status string set when the state is failed or degraded
    std::pmr::string status_string;

    /// Message counts by topic
    std::pmr::unordered_map<std::string_view, size_t> message_counts;

    /// Set of persistent channels that have at least one logged message
    std::pmr::unordered_set<std::string_view> logged_persistent_channels;

    /// Channel message rate counters
    MessageRateCounter message_rate_counter;
  };

  /// Logged channel subscription configuration
  struct SubscriptionConfig
  {
    /// Channel name
    std::string_view channel_name;
    /// UUID string
    std::pmr::string uuid_str;
    /// Slot count
    size_t num_slots{};
    /// Message size in bytes
    size_t message_size_b{};
    /// Channel Type
    ChannelType channel_type{};
  };

  /// Time between calls to the writer's periodic callback when processing messages
  static constexpr std::chrono::milliseconds writer_periodic_callback_interval{5};

  /// Time between attempts to connect any pending subscriptions
  static constexpr std::chrono::seconds poll_pending_subscriptions_interval{1};

  /// Log file prefix
  static constexpr auto log_file_prefix = "onboard_";

public:
  /// Entry in the channel message rates map
  struct ChannelMessageRateMapEntry
  {
    /// Message rate in hz
    double msg_rate_hz;

    /// Message rate status
    RateStatus rate_status;
  };

private:
  /// Construct a LogWriterBase
  /// @param[in] memory_resource Memory resource
  /// @param[in] pinion_shm_root Pinion shared memory root directory
  /// @param[in] pinion_namespace Pinion unix socket namespace
  /// @param[in] log_writer_config Log writer configuration
  /// @param[in] buffer_pool_size Number of buffers in the buffer pool
  /// @param[in] max_log_file_duration Maximum duration a log file will span
  /// @param[in] channel_rates_config Channel message rates config
  LogWriterBase(
    jewels::memory::MemoryResource memory_resource,
    const clockwork::Tappy<LogWriterConfig<>>& log_writer_config,
    std::string_view pinion_shm_root,
    std::string_view pinion_namespace,
    size_t buffer_pool_size,
    std::chrono::nanoseconds max_log_file_duration,
    const clockwork::Tappy<ChannelMessageRatesConfig>& channel_rates_config);

public:
  /// Destructor drains outstanding async writes
  ~LogWriterBase() override;

  // NOLINTNEXTLINE(bugprone-crtp-constructor-accessibility) Conflicts with modernize-use-equals-delete
  LogWriterBase(const LogWriterBase&) = delete;
  // NOLINTNEXTLINE(bugprone-crtp-constructor-accessibility) Conflicts with modernize-use-equals-delete
  LogWriterBase(LogWriterBase&&) = delete;

  LogWriterBase& operator=(const LogWriterBase&) = delete;
  LogWriterBase& operator=(LogWriterBase&&) = delete;

  /// Start logging implementation
  /// @param[in] log_path Path the the output log directory
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> start_logging(std::string_view log_path);

  /// Start logging paused implementation
  /// @param[in] log_path Path the the output log directory
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> start_logging_paused(std::string_view log_path);

  /// Stop logging implementation
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> stop_logging();

  /// Pause logging implementation
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> pause_logging();

  /// Resume logging implementation
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> resume_logging();

  /// Drain async operations implementation
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> drain_async_operations();

  /// Run the logger for a specified duration
  ///
  /// This call blocks waiting for something to do until the duration has elapsed
  ///
  /// @param[in] duration Amount of time to run
  void run_for(std::chrono::nanoseconds duration);

  /// @see ChannelObserverClient::message_callback
  void message_callback(
    jewels::time::SyncTime current_time,
    std::string_view channel_name,
    const ::clockwork::pinion::SlotRef& message_ref) final;

  /// @see ChannelObserverClient::drop_callback
  void drop_callback(std::string_view channel_name, size_t drop_count) final;

  /// Get the current writer state
  /// @note This method *MAY* be called by the thread that reports the writer state
  /// @return Writer state
  [[nodiscard]] LogWriterState get_state();

  /// Get the current status string
  /// @note This method *MAY* be called by the thread that reports the writer state
  /// @return Status string
  [[nodiscard]] std::pmr::string get_status_string();

  /// Check whether this writer is degraded
  /// @note This method *MAY* be called by the thread that reports the writer state
  /// @return true if degraded
  bool get_is_degraded();

  /// Get the log writer status
  /// @note This method *MAY* be called by the thread that reports the writer state
  /// @return Log writer status
  [[nodiscard]] onboard::WriterStatusResult get_status();

  /// Get the dropped message count and reset the drop count to zero
  /// @note This method *MAY* be called by the thread that reports the writer state
  /// @return true if degraded
  [[nodiscard]] size_t get_and_reset_drop_count();

  /// Get the current write backlog
  /// @note This method *MAY* be called by the thread that reports the writer state
  /// @param[in] current_steady_time Current steady time
  /// @return Current write backlog or LogError on failure
  [[nodiscard]] LogExpected<std::chrono::nanoseconds> get_write_backlog(jewels::time::SteadyTime current_steady_time);

  /// Get the message counts by channel
  /// @note This method *MAY* be called by the thread that reports the writer state
  /// @return Message counts by channel
  [[nodiscard]] std::pmr::unordered_map<std::pmr::string, size_t> get_message_counts() const;

  /// Clear the message counts by channel
  /// @note This method *MAY* be called by the thread that reports the writer state
  void clear_message_counts();

  /// Get and reset the maximum write backlog
  /// @note This method *MAY* be called by the thread that reports the writer state
  /// @post The maximum write backlog is reset to zero
  /// @return Write backlog
  [[nodiscard]] std::chrono::nanoseconds get_and_reset_max_write_backlog() noexcept;

  /// Initialize the log writer
  /// @param[in] log_writer_config Log writer configuration
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> initialize(const clockwork::Tappy<LogWriterConfig<>>& log_writer_config);

  /// Get the message rates per channel
  /// @return Map from channel name to channel message rate
  [[nodiscard]] std::pmr::map<std::string_view, ChannelMessageRateMapEntry> get_channel_message_rates();

  /// Get the number of channels with a low message rate
  /// @return Number of channels with a low message rate
  [[nodiscard]] size_t get_num_low_rate_channels() const;

  /// Get the name of a channel with a low message rate
  /// @return Channel name or empty string of no channels have low message rates
  [[nodiscard]] std::string_view get_low_rate_channel_name() const;

protected:
  /// Write a clockwork message to the log from a pinion buffer
  /// @param[in] channel_name Channel name
  /// @param[in] message_handle Clockwork message handle
  /// @param[in] log_time Message log timestamp
  /// @param[in] current_steady_time Current steady time
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> log_message(
    std::string_view channel_name,
    const ::clockwork::pinion::SlotRef& message_handle,
    LogTimestamp log_time,
    jewels::time::SteadyTime current_steady_time);

  /// Write a clockwork message to the log from a pinion buffer blocking as needed to avoid
  /// overrunning the log device
  /// @param[in] channel_name Channel name
  /// @param[in] message_handle Clockwork message handle
  /// @param[in] log_time Message log timestamp
  /// @param[in] current_steady_time Current steady time
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> log_message_wait(
    std::string_view channel_name,
    const ::clockwork::pinion::SlotRef& message_handle,
    LogTimestamp log_time,
    jewels::time::SteadyTime current_steady_time);

  /// Save a persistent message for logging when the writer starts writing again
  /// @param[in] channel_name Channel name
  /// @param[in] message_handle Clockwork message handle
  /// @param[in] log_time Message log timestamp
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> save_persistent_message(
    std::string_view channel_name, const ::clockwork::pinion::SlotRef& message_handle, LogTimestamp log_time);

  /// Write a message to the log
  /// @param[in] message Message to log
  /// @param[in] is_lite_compressed True if the message data is compressed
  /// @param[in] current_steady_time Current steady time
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> log_message(
    const onboard::ZeroCopyMessage& message, bool is_lite_compressed, jewels::time::SteadyTime current_steady_time);

  /// Write a message to the log blocking as needed to avoid
  /// overrunning the log device
  /// @param[in] message Message to log
  /// @param[in] current_steady_time Current steady time
  /// @param[in] is_lite_compressed True if the message data is compressed
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> log_message_wait(
    const onboard::ZeroCopyMessage& message, bool is_lite_compressed, jewels::time::SteadyTime current_steady_time);

  /// Save a persistent message for logging when the writer starts writing again
  /// @param[in] message Message to log
  /// @param[in] is_lite_compressed True if the message data is compressed
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void>
  save_persistent_message(const onboard::ZeroCopyMessage& message, bool is_lite_compressed);

  /// Set the state to failed
  /// @param[in] status_string Human readable status string
  void set_state_to_failed(std::string_view status_string);

  /// Set the is_degraded flag
  /// @param[in] status_string Human readable status string
  void set_is_degraded(std::string_view status_string);

  /// @return True if the channel is persistent
  [[nodiscard]] bool is_persistent_channel(std::string_view channel_name) const;

  /// Buffer pool accessor
  const jewels::memory::NonNullSharedPtr<BufferPoolType>& buffer_pool_ptr() const;

private:
  /// Add the logged channel metadata to the log writer
  /// @param[in] log_writer_config Log writer configuration
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> add_channels(const clockwork::Tappy<LogWriterConfig<>>& log_writer_config);

  /// Try to subscribe to pending logged channels
  void poll_pending_subscriptions();

  /// Poll for messages on the current subscriptions
  /// @param[in] timeout Max time to wait for a message
  void poll_subscriptions(std::chrono::milliseconds timeout);

  /// Update the max write backlog
  /// @param[in] current_steady_time Current steady time
  void update_max_write_backlog(jewels::time::SteadyTime current_steady_time);

  /// Memory resource
  jewels::memory::MemoryResource memory_resource_;

  /// Storage backing channel name string views
  std::pmr::unordered_set<std::pmr::string> channel_name_strings_;

  /// State shared between the writer thread and the thread that reports writer status
  jewels::memory::pmr_unique_ptr<GuardedState> guarded_state_{};

  /// Pinion shared memory root directory
  std::pmr::string pinion_shm_root_;

  /// Pinion unix domain socket namespace
  std::pmr::string pinion_namespace_;

  /// Subscription configuration for each logged channel
  std::pmr::vector<SubscriptionConfig> subscription_configs_;

  /// Logged channel observers
  std::pmr::vector<std::shared_ptr<clockwork::pinion::ChannelObserver>> channel_observer_ptrs_;

  /// Shared memory subscribers for the logged channels
  std::pmr::vector<std::shared_ptr<clockwork::pinion::ShmSubscriber>> shm_subscriber_ptrs_;

  /// List of indexes to the config for the pending subscriptions
  std::pmr::list<size_t> pending_subscriptions_;

  /// Set of persistent channel names
  std::pmr::unordered_set<std::string_view> persistent_channels_;

  /// Buffer pool
  jewels::memory::NonNullSharedPtr<BufferPoolType> buffer_pool_ptr_;

  /// Onboard log writer
  onboard::Writer<OnboardWriterPolicy> writer_;

  /// Epoll manager
  clockwork::EPollManager epoll_;

  /// Shared memory directory open result
  std::shared_ptr<clockwork::pinion::ShmChannelFactory> shm_channel_factory_ptr_;

  /// Last time that we called the writers periodic callback when logging a message
  jewels::time::SteadyTime last_writer_periodic_callback_time_{std::chrono::seconds{0}};

  /// Last time that we polled for pending subscriptions
  jewels::time::SteadyTime last_pending_subscription_poll_time_{std::chrono::seconds{0}};

  /// Flag set when the writer has been initialized
  bool is_initialized_{false};

  /// Number of low rate channels from the last call to get_channel_message_rates
  size_t num_low_rate_channels_{};

  /// Name of a low rate channel from the last call to get_channel_message_rates
  std::pmr::string low_rate_channel_name_;

  friend Derived;
};

} // namespace clockwork_logging

#include "clockwork/logging/writers/log_writer_base.inl"
