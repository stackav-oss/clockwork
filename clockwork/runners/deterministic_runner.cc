// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/runners/deterministic_runner.hh"

#include "clockwork/common/cog_envelope.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/log_cerr/log_time.hh"
#include "jewels/memory/pmr_shared_ptr.hh"
#include "jewels/time/conversions.hh"
#include "jewels/time/sync_time.hh"

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <compare>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <typeinfo>
#include <utility>

namespace clockwork
{
namespace
{

bool occurs_before(
  const jewels::time::SyncTime candidate,
  const std::optional<jewels::time::SyncTime>& first,
  const std::optional<jewels::time::SyncTime>& second)
{
  return (!first || candidate < *first) && (!second || candidate < *second);
}

} // namespace

DeterministicRunner::DeterministicRunner(DeterministicRunnerConfig config)
  : config_(std::move(config)), timers_(config_.resource)
{
  // Set up log_cerr to use a simulated clock
  sim_log_clock_ = jewels::memory::make_pmr_shared<jewels::SimLogClock>(config_.resource);
  jewels::impl::set_log_time_clock(sim_log_clock_);

  queue_ = std::dynamic_pointer_cast<DeterministicCogQueue>(config_.queue);
  if (!queue_)
  {
    throw std::bad_cast();
  }

  for (auto& abstract_timer : config_.timers)
  {
    auto timer = std::dynamic_pointer_cast<DeterministicTimer>(abstract_timer);
    if (!timer)
    {
      throw std::bad_cast();
    }
    timers_.emplace_back(timer);
  }
}

jewels::expected<std::optional<jewels::time::SyncTime>, jewels::MonoError> DeterministicRunner::initialize()
{
  if (config_.channel_publisher)
  {
    auto publisher_status = config_.channel_publisher->initialize();
    if (!publisher_status)
    {
      jewels::log_cerr_error("Failed to initialize channel publisher");
      return jewels::unexpected(jewels::MonoError{});
    }
    return config_.channel_publisher->try_next_message_time();
  }
  return std::nullopt;
}

jewels::time::SyncTime DeterministicRunner::maybe_update_time(const jewels::time::SyncTime& new_time)
{
  // Check that the new time advances current time.  For cogs with a new_message execution condition, this
  // is tied to the commit time of the message publish.
  if (new_time < current_time_)
  {
    return current_time_;
  }
  const auto current_wall_time = jewels::time::SteadyClock::now();
  if (config_.playback_speed > 0.0)
  {
    // Calculate how much simulation time has elapsed vs wall time
    const auto sim_elapsed = new_time - start_time_;
    const auto wall_elapsed = current_wall_time - wall_start_time_;

    // Calculate target wall time based on playback speed
    // playback_speed = 1.0 means sim time == wall time
    // playback_speed = 0.5 means sim should run at half speed (2x wall time)
    // playback_speed = 2.0 means sim should run at double speed (0.5x wall time)
    const auto sim_elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(sim_elapsed).count();
    const auto wall_elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(wall_elapsed).count();
    const auto target_wall_elapsed_ns =
      static_cast<int64_t>(static_cast<double>(sim_elapsed_ns) / config_.playback_speed);

    if (target_wall_elapsed_ns > wall_elapsed_ns)
    {
      const auto sleep_duration = std::chrono::nanoseconds(target_wall_elapsed_ns - wall_elapsed_ns);
      std::this_thread::sleep_for(sleep_duration);
    }
  }

  // Print at 1 Hz
  if (
    static_cast<uint32_t>(
      std::chrono::duration_cast<std::chrono::seconds>(current_wall_time - wall_update_time_).count()) >= 1)
  {
    std::cerr << fmt::format(
                   "INFO Runner Progress: sim elapsed: ({:4.1f} / {:4.1f} s) wall elapsed: ({:3d} s) sim "
                   "time: ({} ns)\n",
                   jewels::time::to_seconds<float>(new_time - start_time_),
                   jewels::time::to_seconds<float>(end_time_ - start_time_),
                   std::chrono::duration_cast<std::chrono::seconds>(current_wall_time - wall_start_time_).count(),
                   jewels::time::get_ns(new_time))
              << std::flush;

    wall_update_time_ = current_wall_time;
  }
  current_time_ = new_time;
  sim_log_clock_->sim_time_nanoseconds = jewels::time::get_ns(current_time_);
  return current_time_;
}

void DeterministicRunner::prepare_and_execute_cog(
  jewels::memory::ObjectPtr<AbstractCog> cog, const jewels::time::SyncTime current_time)
{
  auto throttled_until = jewels::time::SyncTime::min();
  switch (cog->prepare_for_execution(jewels::Out{throttled_until}, current_time).get())
  {
  case CogPrepareResult::ready:
  {
    // Remove before execution because publishing can enqueue downstream Cogs.
    queue_->remove_next();
    auto params = CogExecuteParams{.start_time = current_time, .execution_mode = CogExecutionMode::deterministic};
    // The return value is ignored because all errors are already reported within execute() to the extent that we
    // can report them at present. Some of this reporting/handling is being improved in OI-2593, and more will be
    // done in OI-2730 when we have an improved observability framework.
    std::ignore = cog->execute(params); // TODO(OI-2730): Report errors from execute via observability
    break;
  }
  case CogPrepareResult::not_ready:
    queue_->remove_next();
    break;
  case CogPrepareResult::publisher_throttled:
    queue_->set_throttled_until(cog, throttled_until);
    if (jewels::fails(cog->arm_publisher_throttle_timer(throttled_until)))
    {
      jewels::log_cerr_error("Failed to arm publisher-throttle timer for cog '{}'.", cog->get_name());
    }
    break;
  case CogPrepareResult::states_lock_contention:
  case CogPrepareResult::reentry_lock_contention:
    break;
  }
}

jewels::BinaryOutcome DeterministicRunner::start(
  jewels::time::SyncTime start_time, jewels::time::SyncTime end_time, jewels::cli::ExitCondition& exit)
{
  start_time_ = start_time;
  end_time_ = end_time;
  wall_start_time_ = jewels::time::SteadyClock::now();
  auto current_log_time_status = initialize();
  if (!current_log_time_status)
  {
    jewels::log_cerr_error("DeterministicRunner::start: initialization failed");
    return jewels::failure;
  }
  auto current_log_time = current_log_time_status.value();
  auto current_time = maybe_update_time(start_time);
  update_timers();
  bool publish_error = false;
  while (current_time <= end_time)
  {
    if (exit.check())
    {
      break;
    }

    update_timers();
    auto next_result = queue_->peek();
    auto next_timer_time = get_next_timer_time();

    if (config_.channel_publisher)
    {
      current_log_time = config_.channel_publisher->try_next_message_time();
    }

    if (next_result && occurs_before(next_result->ready_time, current_log_time, next_timer_time))
    {
      // Update the simulated time if ready time is in the future
      current_time = maybe_update_time(next_result->ready_time);
      prepare_and_execute_cog(next_result->cog, current_time);
    }
    else if (
      config_.channel_publisher && current_log_time && (!next_timer_time || (current_log_time < next_timer_time)))
    {
      maybe_update_time(*current_log_time);
      if (!config_.channel_publisher->publish_next_message())
      {
        jewels::log_cerr_error("DeterministicRunner::start: failed to publish message");
        publish_error = true;
        break;
      }
    }
    else if (next_timer_time)
    {
      maybe_update_time(*next_timer_time);
      update_timers();
    }
    else
    {
      break;
    }
  }

  if (publish_error)
  {
    jewels::log_cerr_error("DeterministicRunner::start: failed due to publish error");
    return jewels::failure;
  }

  return jewels::success;
}

std::optional<jewels::time::SyncTime> DeterministicRunner::get_next_timer_time() const
{
  std::optional<jewels::time::SyncTime> min_time;
  for (const auto& timer : timers_)
  {
    if (timer->started())
    {
      if (!min_time)
      {
        min_time.emplace(timer->next_event_time());
      }
      else
      {
        min_time.emplace(std::min(*min_time, timer->next_event_time()));
      }
    }
  }
  return min_time;
}

void DeterministicRunner::update_timers()
{
  for (auto& timer : timers_)
  {
    timer->update(current_time_);
  }
}

} // namespace clockwork
