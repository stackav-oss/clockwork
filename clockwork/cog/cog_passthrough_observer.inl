// IWYU pragma: private, include "clockwork/cog/cog_passthrough_observer.hh"
#pragma once

#include "clockwork/cog/cog_passthrough_observer.hh"

#include "jewels/memory/pointers.hh"

namespace clockwork
{

template <typename CogType>
CogPassthroughObserver<CogType>::CogPassthroughObserver(jewels::memory::ObjectPtr<CogType> cog)
  : cog_(cog)
{
}

template <typename CogType>
void CogPassthroughObserver<CogType>::notify(const Event& event)
{
  cog_->notify(event.current_time);
}

} // namespace clockwork
