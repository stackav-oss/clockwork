// IWYU pragma: private, include "clockwork/aligner/timer_control.hh"
#pragma once
#include "clockwork/aligner/timer_control.hh"

#include "jewels/callsig/outcome.hh"
#include "jewels/time/sync_time.hh"

namespace clockwork::aligner
{

template <typename HandlerType>
AlignerTimerControl<HandlerType>::AlignerTimerControl(HandlerType& handler) noexcept
  : handler_(handler)
{
}

template <typename HandlerType>
jewels::BinaryOutcome AlignerTimerControl<HandlerType>::arm(jewels::time::SyncTime trigger_at) const
{
  return handler_.arm(trigger_at);
}

template <typename HandlerType>
jewels::BinaryOutcome AlignerTimerControl<HandlerType>::disarm() const
{
  return handler_.disarm();
}

template <typename HandlerType>
bool AlignerTimerControl<HandlerType>::is_armed() const
{
  return handler_.is_armed();
}

} // namespace clockwork::aligner
