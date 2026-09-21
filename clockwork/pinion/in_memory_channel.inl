// IWYU pragma: private, include "clockwork/pinion/in_memory_channel.hh"
#pragma once

#include "clockwork/pinion/in_memory_channel.hh"

#include "clockwork/pinion/abstract_channel.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/buffer_layout.hh"
#include "clockwork/pinion/observer.hh"
#include "clockwork/pinion/publisher_slot_ref.hh"
#include "clockwork/pinion/slot_ref.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"

#include <wise_enum.h>

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory_resource>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <utility>
#include <vector>

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
InMemoryChannel<MsgType, num_slots, is_published_once>::InMemoryChannel(
  jewels::memory::MemoryResource resource, size_t max_msgs_per_exec)
  : storage_(jewels::memory::make_pmr_unique<Storage>(resource)),
    buffer_(
      unwrap_try_make<pinion::Buffer>(
        as_writable_bytes(std::span{storage_->bytes}),
        pinion::BufferLayout{
          .num_slots = buffer_layout.num_slots,
          .message_size = buffer_layout.message_size,
          .is_published_once = buffer_layout.is_published_once,
          .max_msgs_per_exec = max_msgs_per_exec,
        })),
    resource_(std::move(resource)),
    observers_(resource_),
    publisher_(jewels::memory::make_non_null_from_ref(*this))
{
}

template <typename MsgType, size_t num_slots, bool is_published_once>
bool InMemoryChannel<MsgType, num_slots, is_published_once>::handshake() noexcept
{
  return true;
}

template <typename MsgType, size_t num_slots, bool is_published_once>
pinion::PublisherHandle InMemoryChannel<MsgType, num_slots, is_published_once>::make_publisher(size_t /*num_observers*/)
{
  return pinion::PublisherHandle{jewels::memory::make_non_null_from_ref(*this)};
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

template <typename MsgType, size_t num_slots, bool is_published_once>
[[nodiscard]] jewels::expected<pinion::PublisherReservation, pinion::ReserveError>
InMemoryChannel<MsgType, num_slots, is_published_once>::reserve(size_t count, bool connected) noexcept
{
  const auto result = buffer_.reserve(count);
  if (!result)
  {
    return jewels::unexpected{result.error()};
  }
  return pinion::PublisherReservation(
    jewels::memory::make_non_null_from_ref(*static_cast<Observer*>(this)),
    jewels::memory::make_non_null_from_ref(buffer_),
    *result,
    count,
    connected);
}

template <typename MsgType, size_t num_slots, bool is_published_once>
pinion::PublisherHandle& InMemoryChannel<MsgType, num_slots, is_published_once>::publisher()
{
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access) This is expected to throw
  return publisher_.value();
}

template <typename MsgType, size_t num_slots, bool is_published_once>
jewels::expected<pinion::PublisherHandle, jewels::MonoError>
InMemoryChannel<MsgType, num_slots, is_published_once>::extract_publisher() noexcept
{
  jewels::expected<pinion::PublisherHandle, jewels::MonoError> result{jewels::unexpected(jewels::MonoError())};
  if (publisher_)
  {
    result.emplace(std::move(*publisher_));
    publisher_.reset();
  }
  return result;
}

template <typename MsgType, size_t num_slots, bool is_published_once>
size_t InMemoryChannel<MsgType, num_slots, is_published_once>::get_publish_count() const noexcept
{
  return buffer_.get_publish_count();
}

template <typename MsgType, size_t num_slots, bool is_published_once>
[[nodiscard]] bool InMemoryChannel<MsgType, num_slots, is_published_once>::on_connect_pending()
{
  return true;
}

template <typename MsgType, size_t num_slots, bool is_published_once>
[[nodiscard]] size_t InMemoryChannel<MsgType, num_slots, is_published_once>::num_clients() const noexcept
{
  return 0;
}

template <typename MsgType, size_t num_slots, bool is_published_once>
void InMemoryChannel<MsgType, num_slots, is_published_once>::notify(const Observer::Event& event)
{
  for (auto observer : observers_)
  {
    observer->notify(event);
  }
}

template <typename MsgType, size_t num_slots, bool is_published_once>
void InMemoryChannel<MsgType, num_slots, is_published_once>::notify(
  AbstractEPollManager& /*epoll*/, int /*efd*/, uint32_t /*events*/)
{
}

template <typename MsgType, size_t num_slots, bool is_published_once>
const pinion::BufferLayout& InMemoryChannel<MsgType, num_slots, is_published_once>::layout() const noexcept
{
  return buffer_.layout();
}

template <typename MsgType, size_t num_slots, bool is_published_once>
std::ranges::subrange<pinion::SlotRef> InMemoryChannel<MsgType, num_slots, is_published_once>::available() const
{
  const auto begin = pinion::SlotRef(jewels::memory::make_non_null_from_ref(buffer_), std::begin(buffer_));
  const auto end = pinion::SlotRef(jewels::memory::make_non_null_from_ref(buffer_), std::end(buffer_));
  return std::ranges::subrange<pinion::SlotRef>{begin, end};
}

template <typename MsgType, size_t num_slots, bool is_published_once>
bool InMemoryChannel<MsgType, num_slots, is_published_once>::add_observer(
  jewels::memory::ObjectPtr<pinion::Observer> observer) noexcept
{
  observers_.emplace_back(std::move(observer));
  return true;
}

template <typename MsgType, size_t num_slots, bool is_published_once>
int InMemoryChannel<MsgType, num_slots, is_published_once>::socket() const noexcept
{
  return -1;
}

template <typename MsgType, size_t num_slots, bool is_published_once>
const std::pmr::string& InMemoryChannel<MsgType, num_slots, is_published_once>::scope() const noexcept
{
  return scope_;
}

template <typename MsgType, size_t num_slots, bool is_published_once>
const std::pmr::string& InMemoryChannel<MsgType, num_slots, is_published_once>::identifier() const noexcept
{
  return identifier_;
}

template <typename MsgType, size_t num_slots, bool is_published_once>
const std::pmr::string& InMemoryChannel<MsgType, num_slots, is_published_once>::name() const noexcept
{
  return name_;
}

} // namespace clockwork
