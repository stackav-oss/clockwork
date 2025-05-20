// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/writers/tests/support/test_publisher.hh"

#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "clockwork/pinion/shm_channel.hh"
#include "clockwork/pinion/shm_channel_factory.hh"
#include "clockwork/pinion/slot.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/std/expected.hh"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <memory>
#include <utility>

namespace clockwork_logging::tests
{

TestPublisher::TestPublisher(
  std::string_view channel_name, std::shared_ptr<clockwork::pinion::ShmPublisher> publisher_ptr)
  : channel_name_(channel_name), publisher_ptr_(std::move(publisher_ptr))
{
}

[[nodiscard]] jewels::expected<TestPublisher, jewels::MonoError> TestPublisher::try_open(
  jewels::memory::MemoryResource memory_resource,
  std::string_view pinion_shm_root,
  std::string_view pinion_namespace,
  std::string_view uuid_str,
  std::string_view channel_name,
  size_t num_slots,
  size_t message_size_b)
{
  auto factory_result = clockwork::pinion::ShmChannelFactory::make(memory_resource, pinion_namespace, pinion_shm_root);
  if (!factory_result)
  {
    jewels::log_cerr_error("Failed to create the shared memory channel factory for {}", channel_name);
    return jewels::unexpected(jewels::MonoError{});
  }
  auto publisher_result = factory_result->open_publisher(
    uuid_str,
    clockwork::pinion::BufferLayout{
      .num_slots = num_slots,
      .message_size = message_size_b,
    },
    1U);
  if (!publisher_result)
  {
    jewels::log_cerr_error(
      "Failed to open the shared memory publisher for {}: {}", channel_name, publisher_result.error());
    return jewels::unexpected(jewels::MonoError{});
  }
  return TestPublisher{channel_name, std::move(publisher_result).value()};
}

[[nodiscard]] bool TestPublisher::on_connect_pending()
{
  if (!publisher_ptr_)
  {
    return false;
  }
  return publisher_ptr_->on_connect_pending();
}

[[nodiscard]] size_t TestPublisher::num_clients() const
{
  if (!publisher_ptr_)
  {
    return 0U;
  }
  return publisher_ptr_->num_clients();
}

[[nodiscard]] jewels::expected<void, jewels::MonoError>
TestPublisher::try_publish(LogTimestamp message_time, std::span<const std::byte> data)
{
  if (!publisher_ptr_)
  {
    jewels::log_cerr_error("Publisher for {} is not open", channel_name_);
    return jewels::unexpected(jewels::MonoError{});
  }
  auto& publisher_handle = publisher_ptr_->publisher();
  auto reserve_result = publisher_handle.reserve();
  if (!reserve_result)
  {
    jewels::log_cerr_error("Failed to reserve a slot for {}", channel_name_);
    return jewels::unexpected(jewels::MonoError{});
  }
  auto& reserved_slot = reserve_result.value();
  auto slot = reserved_slot.slot();
  std::memcpy(slot.message().data(), data.data(), std::min(slot.message().size(), data.size()));
  if (const auto result = reserved_slot.commit(message_time.get_time()); !result)
  {
    jewels::log_cerr_error("Failed to commit slot for {}: {}", channel_name_, result.error());
    return jewels::unexpected(jewels::MonoError{});
  }
  return {};
}

[[nodiscard]] std::string_view TestPublisher::get_channel_name() const noexcept
{
  return channel_name_;
}

[[nodiscard]] std::shared_ptr<clockwork::pinion::ShmPublisher> TestPublisher::underlying_publisher()
{
  return publisher_ptr_;
}

} // namespace clockwork_logging::tests
