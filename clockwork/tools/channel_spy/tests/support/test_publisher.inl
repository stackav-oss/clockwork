// IWYU pragma: private, include "clockwork/tools/channel_spy/tests/support/test_publisher.hh"
#pragma once

#include "clockwork/tools/channel_spy/tests/support/test_publisher.hh"

#include "clockwork/pinion/abstract_channel.hh"
#include "clockwork/pinion/buffer_layout.hh"
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/publisher_slot_ref.hh"
#include "clockwork/pinion/shm_channel.hh"
#include "clockwork/pinion/shm_channel_factory.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"

#include <fmt/format.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <memory_resource>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace clockwork::tools::tests::support
{

template <typename MessageType>
  requires TappyType<MessageType>
TestPublisher<MessageType>::TestPublisher(std::shared_ptr<pinion::AbstractPublisher> publisher_ptr)
  : publisher_ptr_(std::move(publisher_ptr))
{
}

template <typename MessageType>
  requires TappyType<MessageType>
[[nodiscard]] std::shared_ptr<TestPublisher<MessageType>> TestPublisher<MessageType>::open(
  std::string_view pinion_shm_root,
  std::string_view socket_ns,
  std::string_view uuid_str,
  std::string_view channel_name,
  size_t num_slots)
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  auto factory_result = clockwork::pinion::ShmChannelFactory::make(memory_resource, socket_ns, pinion_shm_root);
  if (!factory_result)
  {
    constexpr auto msg = "Failed to create the shared memory channel factory";
    jewels::log_cerr_error("{}", msg);
    throw std::runtime_error(msg);
  }
  auto publisher_result = factory_result->open_publisher(
    uuid_str,
    channel_name,
    clockwork::pinion::BufferLayout{
      .num_slots = num_slots,
      .message_size = sizeof(MessageType),
      .is_published_once = false,
    },
    1U);
  if (!publisher_result)
  {
    const auto msg = fmt::format("Failed to open the shared memory publisher: {}", publisher_result.error());
    jewels::log_cerr_error("{}", msg);
    throw std::runtime_error(msg);
  }
  return std::shared_ptr<TestPublisher>{new TestPublisher{std::move(publisher_result).value()}};
}

template <typename MessageType>
  requires TappyType<MessageType>
[[nodiscard]] jewels::expected<void, jewels::MonoError>
TestPublisher<MessageType>::publish(int64_t message_time, const MessageType& message)
{
  return publish(message_time, std::as_bytes(std::span{&message, 1U}));
}

template <typename MessageType>
  requires TappyType<MessageType>
[[nodiscard]] jewels::expected<void, jewels::MonoError>
TestPublisher<MessageType>::publish(int64_t message_time, std::span<const std::byte> data)
{
  if (data.size() != sizeof(MessageType))
  {
    jewels::log_cerr_error("Invalid message size");
    return jewels::unexpected(jewels::MonoError{});
  }
  if (!publisher_ptr_)
  {
    jewels::log_cerr_error("Publisher is not open");
    return jewels::unexpected(jewels::MonoError{});
  }
  auto& publisher_handle = publisher_ptr_->publisher();
  auto reserve_result = publisher_handle.reserve();
  if (!reserve_result)
  {
    jewels::log_cerr_error("Failed to reserve a slot");
    return jewels::unexpected(jewels::MonoError{});
  }
  auto& reservation = reserve_result.value();
  auto slot = reservation.slots().front();
  std::memcpy(slot.message().data(), data.data(), data.size());
  if (const auto result = reservation.commit(jewels::time::SyncTime{std::chrono::nanoseconds(message_time)}); !result)
  {
    jewels::log_cerr_error("Failed to commit slot", result.error());
    return jewels::unexpected(jewels::MonoError{});
  }
  return {};
}

} // namespace clockwork::tools::tests::support
