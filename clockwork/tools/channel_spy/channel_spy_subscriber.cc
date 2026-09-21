// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/tools/channel_spy/channel_spy_subscriber.hh"

#include "clockwork/pinion/buffer_layout.hh"
#include "clockwork/pinion/shm_channel.hh"
#include "clockwork/pinion/shm_channel_factory.hh"
#include "clockwork/pinion/slot.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"

#include <fmt/format.h>

#include <cstddef>
#include <iterator>
#include <memory_resource>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace clockwork::tools
{

[[nodiscard]] std::unique_ptr<ChannelSpySubscriber> ChannelSpySubscriber::make_subscriber(
  std::string_view shm_dir,
  std::string_view socket_ns,
  std::string_view uuid_str,
  std::string_view channel_name,
  size_t num_slots,
  size_t message_size,
  GenericCallbackFunction callback_fn)
{
  const pinion::BufferLayout buffer_layout{
    .num_slots = num_slots,
    .message_size = message_size,
    .is_published_once = false,
  };
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  auto factory_result = clockwork::pinion::ShmChannelFactory::make(memory_resource, socket_ns, shm_dir);
  if (!factory_result)
  {
    const auto* const msg = "Failed to make channel factory";
    jewels::log_cerr_error("{}", msg);
    throw std::runtime_error(msg);
  }
  auto open_result = factory_result->open_spy(uuid_str, channel_name, buffer_layout, 1U);
  if (!open_result)
  {
    const auto msg = fmt::format("Failed to open shared memory channel: {}", open_result.error());
    jewels::log_cerr_error("{}", msg);
    throw std::runtime_error(msg);
  }
  return std::unique_ptr<ChannelSpySubscriber>{
    new ChannelSpySubscriber{std::move(open_result).value(), std::move(callback_fn)}};
}

[[nodiscard]] std::unique_ptr<ChannelSpySubscriber> ChannelSpySubscriber::make_subscriber(
  std::string_view shm_dir,
  std::string_view socket_ns,
  std::string_view uuid_str,
  std::string_view channel_name,
  size_t num_slots,
  size_t message_size,
  RawMessageCallback callback_fn)
{
  return make_subscriber(
    shm_dir,
    socket_ns,
    uuid_str,
    channel_name,
    num_slots,
    message_size,
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks) There is no leak here
    [cb_fn = std::move(callback_fn)](
      uint64_t sequence_number, int64_t message_time, std::span<const std::byte> data, pinion::SlotRef slot_ref)
    { cb_fn(sequence_number, message_time, data, [&slot_ref]() { return slot_ref.is_valid(); }); });
}

[[nodiscard]] std::unique_ptr<ChannelSpySubscriber> ChannelSpySubscriber::make_subscriber(
  std::string_view shm_dir,
  std::string_view socket_ns,
  std::string_view uuid_str,
  std::string_view channel_name,
  size_t num_slots,
  size_t message_size,
  PythonCallback callback_fn)
{
  return make_subscriber(
    shm_dir,
    socket_ns,
    uuid_str,
    channel_name,
    num_slots,
    message_size,
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks) There is no leak here
    [cb_fn = std::move(callback_fn)](
      uint64_t sequence_number, int64_t message_time, std::span<const std::byte> data, pinion::SlotRef slot_ref)
    { cb_fn(PythonCallbackHandle{sequence_number, message_time, data, slot_ref}); });
}

void ChannelSpySubscriber::poll()
{
  const auto available = subscriber_->available();
  if (available.empty())
  {
    return;
  }
  const auto newest_iter = std::prev(available.end());
  if (!last_iter_.is_sentinel() && last_iter_ == newest_iter)
  {
    return;
  }
  last_iter_ = newest_iter;
  const auto slot = *last_iter_;
  const auto sequence_number = slot.header()->sequence_number;
  const auto message_time = slot.header()->publish_timestamp;
  callback_fn_(sequence_number, message_time, slot.message(), newest_iter);
}

ChannelSpySubscriber::ChannelSpySubscriber(
  std::shared_ptr<pinion::AbstractSubscriber> subscriber, GenericCallbackFunction callback_fn)
  : subscriber_(std::move(subscriber)), callback_fn_(std::move(callback_fn))
{
}

} // namespace clockwork::tools
