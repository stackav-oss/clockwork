// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/cog/wrappers/dummy_cog_queue.hh"

#include "clockwork/common/cog_envelope.hh"
#include "jewels/std/expected.hh"

#include <chrono>

namespace clockwork::testing
{

void DummyCogQueue::notify() {}

void DummyCogQueue::push(CogEnvelope /*envelope*/) {}

AbstractCogQueue::PopResult DummyCogQueue::pop(std::chrono::nanoseconds /*timeout*/)
{
  return jewels::unexpected(jewels::MonoError{});
}

[[nodiscard]] CogQueueStats DummyCogQueue::stats() const
{
  return {};
}

[[nodiscard]] bool DummyCogQueue::is_offline() const
{
  return true;
}

} // namespace clockwork::testing
