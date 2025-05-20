// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/abstract_cog.hh"

#include "clockwork/common/abstract_cog_queue.hh"
#include "clockwork/common/cog_envelope.hh"
#include "jewels/memory/pointers.hh"

namespace clockwork
{

AbstractCog::AbstractCog(jewels::memory::ObjectPtr<AbstractCogQueue> queue)
  : queue_(queue)
{
}

AbstractCog::~AbstractCog() = default;

void AbstractCog::notify_ready_queue()
{
  queue_->notify();
}

void AbstractCog::add_to_ready_queue(jewels::time::SyncTime ready_time)
{
  queue_->push(CogEnvelope{.ready_time = ready_time, .cog = jewels::memory::make_non_null_from_ref(*this)});
}

} // namespace clockwork
