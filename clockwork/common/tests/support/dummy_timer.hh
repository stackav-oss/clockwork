// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/common/abstract_timer.hh"
#include "jewels/time/sync_time.hh"

#include <chrono>
#include <cstdint>

namespace clockwork::testing
{

/// DummyTimer used by unit test cogs
class DummyTimer : public AbstractTimer
{
public:
  explicit DummyTimer() noexcept = default;

  ///
  /// Destructor.
  ///
  ~DummyTimer() override = default;

  DummyTimer(const DummyTimer&) = delete;
  DummyTimer& operator=(const DummyTimer&) = delete;
  DummyTimer(DummyTimer&&) = delete;
  DummyTimer& operator=(DummyTimer&&) = delete;

  /// @see AbstractEPollCallback::notify
  void notify(AbstractEPollManager& epoll, int efd, uint32_t events) override;

  /// @see AbstractTimer::descriptor
  [[nodiscard]] int32_t descriptor() const override;

  /// @see AbstractTimer::set_observer
  void set_observer(pinion::Observer* observer) override;

  /// @see AbstractTimer::start
  [[nodiscard]] bool start(jewels::time::SyncTime trigger_at, std::chrono::nanoseconds period) override;

  /// @see AbstractTimer::stop
  [[nodiscard]] bool stop() override;
};

} // namespace clockwork::testing
