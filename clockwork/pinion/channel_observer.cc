// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/pinion/channel_observer.hh"

#include "clockwork/pinion/slot_ref.hh"

#include <cstddef>
#include <iterator>
#include <ranges>
#include <utility>

namespace clockwork::pinion
{

ChannelObserver::ChannelObserver(
  ::jewels::memory::MemoryResource memory_resource,
  std::shared_ptr<AbstractChannel> subscriber,
  ::jewels::memory::ObjectPtr<ChannelObserverClient> client_ptr,
  std::string_view channel_name,
  ::clockwork_logging::ChannelType channel_type)
  : mem_res_(memory_resource),
    subscriber_(std::move(subscriber)),
    client_ptr_(client_ptr),
    channel_name_(channel_name, memory_resource),
    channel_type_(channel_type)
{
}

void ChannelObserver::notify(const ::clockwork::pinion::Observer::Event& event)
{
  const auto available = subscriber_->available();
  if (next_iterator_.is_sentinel())
  {
    if (available.begin() == available.end())
    {
      next_iterator_ = available.begin();
    }
    else if (channel_type_ == ::clockwork_logging::ChannelType::persistent)
    {
      next_iterator_ = std::prev(available.end());
    }
    else
    {
      next_iterator_ = available.end();
    }
  }
  if (next_iterator_ < available.begin())
  {
    const auto drop_count = static_cast<size_t>(std::distance(next_iterator_, available.begin()));
    client_ptr_->drop_callback(channel_name_, drop_count);
    next_iterator_ = available.begin();
  }
  while (next_iterator_ != available.end())
  {
    client_ptr_->message_callback(event.current_time, channel_name_, next_iterator_);
    ++next_iterator_;
  }
}

[[nodiscard]] std::string_view ChannelObserver::get_channel_name() const noexcept
{
  return channel_name_;
}

} // namespace clockwork::pinion
