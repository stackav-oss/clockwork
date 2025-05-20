// IWYU pragma: private, include "clockwork/cog/input_condition.hh"
#pragma once

#include "clockwork/cog/input_condition.hh"

#include "clockwork/dial/cond_messages_present.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "jewels/std/expected.hh"

#include <boost/iterator/iterator_facade.hpp>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <ranges>
#include <utility>

namespace clockwork
{

template <typename Policy>
InputCondition<Policy>::InputCondition(pinion::SubscriberHandle subscriber) noexcept
  : subscriber_(std::move(subscriber))
{
}

template <typename Policy>
bool InputCondition<Policy>::validate() const
{
  return true;
}

template <typename Policy>
jewels::expected<std::ranges::subrange<pinion::BufferIterator>, pinion::ProgressError>
InputCondition<Policy>::available_range() const
{
  auto available = subscriber_.available();
  switch (Policy::condition_type)
  {
  case InputConditionType::any_message:
    return available;
  case InputConditionType::new_message:
    if (is_sentinel_iterator(last_viewed_) || (last_viewed_ < available.begin()))
    {
      return available;
    }
    return pinion::available_starting_from(available, last_viewed_);
  }
}

template <typename Policy>
auto InputCondition<Policy>::make_condition() -> ConditionType
{
  if (auto range = this->available_range())
  {
    auto size = static_cast<uint32_t>(range->size());
    auto ready = (size >= bounds_min);
    auto num_messages = std::min(size, bounds_max);
    return MessagePresentCondition<bounds_min, bounds_max>(ready, num_messages);
  }

  return MessagePresentCondition<bounds_min, bounds_max>(false, 0);
}

template <typename Policy>
void InputCondition<Policy>::commit(pinion::BufferIterator last_viewed)
{
  last_viewed_ = last_viewed;
}

} // namespace clockwork
