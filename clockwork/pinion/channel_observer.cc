// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/pinion/channel_observer.hh"

#include <boost/iterator/iterator_facade.hpp>

#include <cstddef>
#include <iterator>

namespace clockwork::pinion
{

ChannelObserver::ChannelObserver(
  jewels::memory::MemoryResource memory_resource,
  jewels::memory::ObjectPtr<clockwork::pinion::Buffer> buffer_ptr,
  jewels::memory::ObjectPtr<ChannelObserverClient> client_ptr,
  std::string_view channel_name,
  clockwork_logging::ChannelType channel_type)
  : buffer_ptr_(buffer_ptr),
    client_ptr_(client_ptr),
    channel_name_(channel_name, memory_resource),
    channel_type_(channel_type)
{
}

void ChannelObserver::notify(const clockwork::pinion::Observer::Event& event)
{
  const auto buffer_end = std::end(*buffer_ptr_);
  const auto buffer_begin = std::begin(*buffer_ptr_);
  if (clockwork::pinion::is_sentinel_iterator(next_iterator_))
  {
    if (buffer_begin == buffer_end)
    {
      next_iterator_ = buffer_begin;
    }
    else if (channel_type_ == clockwork_logging::ChannelType::persistent)
    {
      next_iterator_ = std::prev(buffer_end);
    }
    else
    {
      next_iterator_ = buffer_end;
    }
  }
  if (next_iterator_ < buffer_begin)
  {
    const auto drop_count = static_cast<size_t>(next_iterator_.distance_to(buffer_begin));
    client_ptr_->drop_callback(channel_name_, drop_count);
    next_iterator_ = buffer_begin;
  }
  while (next_iterator_ != buffer_end)
  {
    client_ptr_->message_callback(event.current_time, channel_name_, buffer_ptr_, next_iterator_);
    ++next_iterator_;
  }
}

[[nodiscard]] std::string_view ChannelObserver::get_channel_name() const noexcept
{
  return channel_name_;
}

} // namespace clockwork::pinion
