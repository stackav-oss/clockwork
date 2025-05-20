// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/tests/support/test_cog.hh"

#include "clockwork/common/cog_envelope.hh"
#include "clockwork/common/cog_execution_error.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"

#include <stdexcept>
#include <thread>
#include <utility>

namespace clockwork
{

bool operator==(const TestMsg& lhs, const TestMsg& rhs)
{
  return lhs.value == rhs.value;
}

void NullCogQueue::notify() {}

void NullCogQueue::push(CogEnvelope /*envelope*/) {}

auto NullCogQueue::pop(std::chrono::nanoseconds timeout) -> PopResult
{
  std::this_thread::sleep_for(timeout);
  return jewels::unexpected(jewels::MonoError{});
}

CogQueueStats NullCogQueue::stats() const
{
  return CogQueueStats{.size = 0};
}

TestCog::TestCog(jewels::memory::ObjectPtr<AbstractCogQueue> queue)
  : TestCog(queue, std::make_shared<std::shared_mutex>())
{
}

TestCog::TestCog(
  jewels::memory::ObjectPtr<AbstractCogQueue> queue, std::shared_ptr<std::shared_mutex> shared_state_mutex)
  : AbstractCog(queue), shared_state_mutex_(std::move(shared_state_mutex))
{
  if (!shared_state_mutex_)
  {
    throw std::invalid_argument("TestCog shared_state_mutex must be non-null.");
  }
}

TestCog::~TestCog() = default;

std::string_view TestCog::get_name() const
{
  return "clockwork::TestCog";
}

jewels::expected<void, jewels::MonoError> TestCog::prime(jewels::time::SyncTime /*start_time*/)
{
  return {};
}

jewels::expected<void, CogExecutionError> TestCog::prepare_for_execution(jewels::time::SyncTime /*current_time*/)
{
  auto reentry_lock = std::unique_lock(reentry_mutex_, std::defer_lock);

  if (!reentry_lock.try_lock())
  {
    return jewels::unexpected(CogExecutionError::reentry_lock_contention);
  }

  auto shared_state_lock = std::unique_lock(*shared_state_mutex_, std::defer_lock);
  if (!shared_state_lock.try_lock())
  {
    return jewels::unexpected(CogExecutionError::states_lock_contention);
  }

  shared_state_lock_ = std::move(shared_state_lock);
  reentry_lock_ = std::move(reentry_lock);

  return {};
}

jewels::expected<void, CogExecutionError> TestCog::execute(CogExecuteParams params)
{
  std::unique_lock lock(internals_mutex_);

  auto msg = std::optional<TestMsg>();
  if (!msgs_.empty())
  {
    msg = std::make_optional(msgs_.front());
    msgs_.pop();
  }

  if (execute_callback_)
  {
    lock.unlock();
    execute_callback_(params, msg);
  }

  // Unwind locks

  shared_state_lock_ = {};
  reentry_lock_ = {};

  // Notify the queue we have completed.

  notify_ready_queue();
  return {};
}

void TestCog::push(TestMsg msg)
{
  const std::lock_guard lock(internals_mutex_);

  msgs_.push(msg);
}

void TestCog::set_execute_callback(ExecuteCallback callback)
{
  execute_callback_ = std::move(callback);
}

void TestCog::signal_is_ready(jewels::time::SyncTime ready_time)
{
  add_to_ready_queue(ready_time);
}

} // namespace clockwork
