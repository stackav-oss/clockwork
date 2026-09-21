// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/pinion/abstract_channel.hh"

#include <utility>

namespace clockwork::pinion
{

AbstractChannel::~AbstractChannel() = default;

PublisherHandle::PublisherHandle(jewels::memory::ObjectPtr<AbstractPublisher> publisher)
  : publisher_(std::move(publisher))
{
}

const BufferLayout& PublisherHandle::layout() const noexcept
{
  return publisher_->layout();
}

size_t PublisherHandle::get_publish_count() const noexcept
{
  return publisher_->get_publish_count();
}

bool PublisherHandle::add_observer(jewels::memory::ObjectPtr<Observer> observer) noexcept
{
  return publisher_->add_observer(observer);
}

jewels::expected<PublisherReservation, ReserveError> PublisherHandle::reserve(bool connected) noexcept
{
  return reserve(1, connected);
}

jewels::expected<PublisherReservation, ReserveError> PublisherHandle::reserve(size_t count) noexcept
{
  return reserve(count, true);
}

jewels::expected<PublisherReservation, ReserveError> PublisherHandle::reserve(size_t count, bool connected) noexcept
{
  return publisher_->reserve(count, connected);
}

} // namespace clockwork::pinion
