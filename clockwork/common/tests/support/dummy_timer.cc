// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/tests/support/dummy_timer.hh"

#include "jewels/time/sync_time.hh"

#include <chrono>
#include <cstdint>

namespace clockwork::testing
{

void DummyTimer::notify(AbstractEPollManager& /*epoll*/, int /*efd*/, uint32_t /*events*/) {}

[[nodiscard]] int32_t DummyTimer::descriptor() const
{
  return -1;
}

void DummyTimer::set_observer(pinion::Observer* /*observer*/) {}

[[nodiscard]] bool DummyTimer::start(jewels::time::SyncTime /*trigger_at*/, std::chrono::nanoseconds /*period*/)
{
  return true;
}

[[nodiscard]] bool DummyTimer::stop()
{
  return true;
}

} // namespace clockwork::testing
