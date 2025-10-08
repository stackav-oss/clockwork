// IWYU pragma: private, include "clockwork/tools/channel_spy/channel_spy_subscriber.hh"
#pragma once

#include "clockwork/tools/channel_spy/channel_spy_subscriber.hh"

#include "clockwork/repr_iface.hh"
#include "clockwork/tools/channel_spy/types.hh"
#include "jewels/log_cerr/log_cerr.hh"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string_view>
#include <utility>

namespace clockwork::tools
{

template <typename MessageType>
[[nodiscard]] std::unique_ptr<ChannelSpySubscriber> ChannelSpySubscriber::make_subscriber(
  std::string_view shm_dir,
  std::string_view socket_ns,
  std::string_view uuid_str,
  std::string_view channel_name,
  size_t num_slots,
  DeserializedMessageCallback<MessageType> callback_fn)
  requires(TappyType<MessageType> || TachyonType<MessageType>)
{
  return make_subscriber(
    shm_dir,
    socket_ns,
    uuid_str,
    channel_name,
    num_slots,
    sizeof(MessageType),
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks) There is no leak here
    [cb_fn = std::move(callback_fn)](
      uint64_t sequence_number,
      int64_t message_time,
      std::span<const std::byte> data,
      const std::function<bool()>& overrun_check_fn)
    {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) There is no leak here
      auto message_ptr = std::make_unique<MessageType>(*reinterpret_cast<const MessageType*>(data.data()));
      if (!overrun_check_fn())
      {
        jewels::log_cerr_warn("Detected overrun in callback");
        return;
      }
      cb_fn(sequence_number, message_time, std::move(message_ptr));
    });
}

} // namespace clockwork::tools
