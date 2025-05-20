// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/pinion/subscriber_handle.hh"

#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/error.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"

#include <boost/iterator/iterator_facade.hpp>

#include <iterator>

namespace clockwork::pinion
{

SubscriberHandle::SubscriberHandle(jewels::memory::ObjectPtr<const Buffer> buffer)
  : buffer_{buffer}
{
}

const BufferLayout& SubscriberHandle::layout() const noexcept
{
  return buffer_->layout();
}

std::ranges::subrange<BufferIterator> SubscriberHandle::available() const
{
  // Assigning to variables to make evaluation order explicit because
  // for this range calling begin() and end() use atomic operations.
  // Begin should be called before end. Worst case, publisher adds new
  // messages causing begin to update between the calls of begin and
  // end.  This same thing can also happen after this function
  // returns.  Users should always check still_available to make sure
  // data is valid regardless.
  const auto begin = std::begin(*buffer_);
  const auto end = std::end(*buffer_);
  return std::ranges::subrange<BufferIterator>{begin, end};
}

bool SubscriberHandle::still_available(const BufferIterator& iterator) const
{
  // This should only ever happen on startup when a subscriber has not yet consumed a message.
  if (is_sentinel_iterator(iterator))
  {
    return false;
  }
  // Only need to check against begin because the subscriber cannot be ahead of the publisher.
  return std::begin(*buffer_) <= iterator;
}

jewels::expected<std::ranges::subrange<BufferIterator>, ProgressError>
available_starting_from(const std::ranges::subrange<BufferIterator>& available, const BufferIterator& next_to_consume)
{
  // This should only ever happen on startup when a subscriber has not yet consumed a message.
  if (is_sentinel_iterator(next_to_consume))
  {
    return available;
  }
  if (next_to_consume < std::begin(available))
  {
    return jewels::unexpected{ProgressError::fell_behind};
  }
  if (next_to_consume > std::end(available))
  {
    return jewels::unexpected{ProgressError::in_the_future};
  }
  return std::ranges::subrange<BufferIterator>{next_to_consume, std::end(available)};
}

} // namespace clockwork::pinion
