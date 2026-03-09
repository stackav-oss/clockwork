// IWYU pragma: private, include "clockwork/pinion/in_memory_channel.hh"
#pragma once

#include "clockwork/pinion/in_memory_channel.hh"

#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/memory/pointers.hh"

#include <wise_enum.h>

#include <cstddef>
#include <optional>
#include <span>
#include <utility>

namespace clockwork
{

template <class T, class... Args>
auto unwrap_try_make(Args&&... args)
{
  auto expected = T::try_make(std::forward<Args>(args)...);
  if (!expected)
  {
    jewels::log_cerr_error("Failed to make buffer: {}", wise_enum::to_string(expected.error()));
    throw std::bad_optional_access();
  }
  return *std::move(expected);
}

template <typename MsgType, size_t num_slots, bool is_published_once>
InMemoryChannel<MsgType, num_slots, is_published_once>::InMemoryChannel(jewels::memory::MemoryResource resource)
  : storage_(jewels::memory::make_pmr_unique<Storage>(resource)),
    buffer_(unwrap_try_make<pinion::Buffer>(as_writable_bytes(std::span{storage_->bytes}), layout)),
    resource_(std::move(resource))
{
}

template <typename MsgType, size_t num_slots, bool is_published_once>
pinion::PublisherHandle InMemoryChannel<MsgType, num_slots, is_published_once>::make_publisher(size_t num_observers)
{
  return pinion::PublisherHandle{jewels::memory::make_non_null_from_ref(buffer_), num_observers, resource_};
}

template <typename MsgType, size_t num_slots, bool is_published_once>
pinion::SubscriberHandle InMemoryChannel<MsgType, num_slots, is_published_once>::make_subscriber()
{
  return pinion::SubscriberHandle{jewels::memory::make_non_null_from_ref(buffer_)};
}

template <typename MsgType, size_t num_slots, bool is_published_once>
pinion::Buffer& InMemoryChannel<MsgType, num_slots, is_published_once>::buffer()
{
  return buffer_;
}

template <typename MsgType, size_t num_slots, bool is_published_once>
const pinion::Buffer& InMemoryChannel<MsgType, num_slots, is_published_once>::buffer() const
{
  return buffer_;
}

} // namespace clockwork
