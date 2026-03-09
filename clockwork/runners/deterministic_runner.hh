// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "clockwork/common/abstract_cog.hh"
#include "clockwork/common/abstract_cog_queue.hh"
#include "clockwork/common/abstract_timer.hh"
#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/pinion/shm_publisher.hh"
#include "clockwork/runners/deterministic_cog_queue.hh"
#include "clockwork/runners/deterministic_timer.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/cli/exit_condition.hh"
#include "jewels/container/compare.hh"
#include "jewels/log_cerr/log_time.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"
#include "jewels/uuid/uuid_hasher.hh"

#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <memory_resource>
#include <optional>
#include <unordered_map>
#include <vector>

namespace clockwork
{
using ChannelMap = std::pmr::unordered_map<
  jewels::Uuid<::clockwork::common::EndpointInstanceId>,
  std::shared_ptr<::clockwork::pinion::ShmPublisher>,
  jewels::UuidHasher<::clockwork::common::EndpointInstanceId>>;

///
/// An interface for things that publish onto channels.
///
class AbstractChannelPublisher
{
public:
  AbstractChannelPublisher() = default;
  virtual ~AbstractChannelPublisher() = default;

  AbstractChannelPublisher(const AbstractChannelPublisher&) = delete;
  AbstractChannelPublisher& operator=(const AbstractChannelPublisher&) = delete;
  AbstractChannelPublisher(AbstractChannelPublisher&&) = delete;
  AbstractChannelPublisher& operator=(AbstractChannelPublisher&&) = delete;

  ///
  /// Attempt to initialize the publisher.
  ///
  virtual jewels::expected<void, jewels::MonoError> initialize() = 0;

  ///
  /// Get the time the next message should be published.
  /// @return time the next message should be published or std::nullopt if there are no pending messages.
  ///
  virtual std::optional<jewels::time::SyncTime> try_next_message_time() = 0;

  ///
  /// Attempt to publish the next message.
  ///
  virtual jewels::expected<void, jewels::MonoError> publish_next_message() = 0;

  ///
  /// @return true if there are messages remaining to be published.
  /// @return false if no messages remain to be published.
  ///
  virtual bool messages_remaining() = 0;
};

///
/// Configuration for the deterministic runner.
///
struct DeterministicRunnerConfig
{
  /// A memory resource for allocating timer storage.
  jewels::memory::MemoryResource resource;
  /// The cogs to be executed by the runner.
  std::pmr::vector<CogConfig> cogs;
  /// The timers to be managed by the runner.
  std::pmr::vector<std::shared_ptr<AbstractTimer>> timers;
  /// Queue for cog executions.
  std::shared_ptr<AbstractCogQueue> queue;
  /// Mapping for channels to publishers.
  ChannelMap channel_map{};
  /// Time window to execute the runner within.
  jewels::time::SyncTime start_time;
  jewels::time::SyncTime end_time;
  std::shared_ptr<AbstractChannelPublisher> channel_publisher;
  /// Map from cogs to assigned GPU
  std::pmr::unordered_map<jewels::memory::ObjectPtr<AbstractCog>, int16_t> cog_to_gpu_id;
  /// Playback speed multiplier (1.0 = real-time, 0.5 = half speed, 2.0 = double speed).
  /// When 0.0 (default), runs as fast as possible.
  /// Experimental flag controlled via --playback-speed; may be removed once validated.
  double playback_speed = 0.0;
};

class DeterministicRunner
{
public:
  ///
  /// Constructor.
  ///
  explicit DeterministicRunner(DeterministicRunnerConfig config);

  ///
  /// Destructor.
  ///
  ~DeterministicRunner() = default;

  DeterministicRunner(const DeterministicRunner&) = delete;
  DeterministicRunner& operator=(const DeterministicRunner&) = delete;
  DeterministicRunner(DeterministicRunner&&) = delete;
  DeterministicRunner& operator=(DeterministicRunner&&) = delete;

  ///
  /// Run the system
  /// @param[in] start_time The simulation start time
  /// @param[in] end_time The simulation end time; this function returns when the simulated clock reaches this time
  /// @return BinaryOutcome indicating success or failure
  ///
  jewels::BinaryOutcome
  start(jewels::time::SyncTime start_time, jewels::time::SyncTime end_time, jewels::cli::ExitCondition& exit);

private:
  ///
  /// Initialize the system
  /// @return  The time of the first expected message in the publisher.
  ///
  jewels::expected<std::optional<jewels::time::SyncTime>, jewels::MonoError> initialize();

  void update_timers();
  [[nodiscard]] std::optional<jewels::time::SyncTime> get_next_timer_time() const;
  jewels::time::SyncTime maybe_update_time(const jewels::time::SyncTime& new_time);

  DeterministicRunnerConfig config_;
  std::pmr::list<std::shared_ptr<DeterministicTimer>> timers_;
  std::shared_ptr<DeterministicCogQueue> queue_;
  jewels::time::SyncTime start_time_;
  jewels::time::SyncTime end_time_;
  jewels::time::SyncTime current_time_;
  jewels::time::SteadyTime wall_start_time_;
  jewels::time::SteadyTime wall_update_time_;
  std::shared_ptr<jewels::SimLogClock> sim_log_clock_;
};

} // namespace clockwork
