// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/common/abstract_cog.hh"
#include "clockwork/common/abstract_cog_queue.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/time/sync_time.hh"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <shared_mutex>
#include <string_view>

namespace clockwork
{

struct TestMsg
{
  int32_t value;
};

bool operator==(const TestMsg& lhs, const TestMsg& rhs);

///
/// Null cog queue used for testing, it never actually does anything.
///
class NullCogQueue : public AbstractCogQueue
{
public:
  void notify() override;
  void push(CogEnvelope envelope) override;
  PopResult pop(std::chrono::nanoseconds timeout) override;
  [[nodiscard]] CogQueueStats stats() const override;
  [[nodiscard]] bool is_offline() const override;
};

///
/// Test cog that allows the user to manually add messages and trigger ready.
///
class TestCog : public AbstractCog
{
public:
  using ExecuteCallback = std::function<void(const CogExecuteParams&, const std::optional<TestMsg>&)>;

  ///
  /// @param[in] queue The cog queue to push onto when ready.
  ///
  explicit TestCog(jewels::memory::ObjectPtr<AbstractCogQueue> queue);
  ///
  /// @param[in] queue The cog queue to push onto when ready.
  /// @param[in] shared_state_mutex The shared_mutex to use to simulate cogs with shared state.
  ///
  explicit TestCog(
    jewels::memory::ObjectPtr<AbstractCogQueue> queue, std::shared_ptr<std::shared_mutex> shared_state_mutex);
  ~TestCog() override;
  TestCog(const TestCog&) = delete;
  TestCog& operator=(const TestCog&) = delete;
  TestCog(TestCog&&) = delete;
  TestCog& operator=(TestCog&&) = delete;

  [[nodiscard]] std::string_view get_name() const override;

  jewels::expected<void, jewels::MonoError> prime(jewels::time::SyncTime /*start_time*/) override;
  jewels::expected<void, CogExecutionError> prepare_for_execution(jewels::time::SyncTime /*current_time*/) override;
  jewels::expected<void, CogExecutionError> execute(CogExecuteParams params) override;

  void push(TestMsg msg);
  void set_execute_callback(ExecuteCallback callback);
  void signal_is_ready(jewels::time::SyncTime ready_time);

private:
  std::mutex reentry_mutex_;
  std::unique_lock<std::mutex> reentry_lock_;

  std::shared_ptr<std::shared_mutex> shared_state_mutex_;
  std::unique_lock<std::shared_mutex> shared_state_lock_;

  std::mutex internals_mutex_;
  std::queue<TestMsg> msgs_;
  ExecuteCallback execute_callback_;
};

} // namespace clockwork
