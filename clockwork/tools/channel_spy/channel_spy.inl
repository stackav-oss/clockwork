// IWYU pragma: private, include "clockwork/tools/channel_spy/channel_spy.hh"
#pragma once

#include "clockwork/tools/channel_spy/channel_spy.hh"

#include "clockwork/repr_iface.hh"
#include "clockwork/tools/channel_spy/channel_spy_config_clk_cc.hh"
#include "clockwork/tools/channel_spy/channel_spy_subscriber.hh"
#include "clockwork/tools/channel_spy/types.hh"
#include "jewels/container/compare.hh"
#include "jewels/uuid/uuid.hh"

#include <memory>
#include <span>
#include <string_view>

namespace clockwork::tools
{

template <typename MessageType>
void ChannelSpy::subscribe(std::string_view channel_name, const DeserializedMessageCallback<MessageType>& callback_fn)
  requires(TappyType<MessageType> || TachyonType<MessageType>)
{
  const auto config_ptr = read_channel_spy_config();
  for (const auto& channel : config_ptr->get_channels())
  {
    if (channel.get_channel_name() == channel_name)
    {
      subscribers_.emplace_back(
        ChannelSpySubscriber::make_subscriber<MessageType>(
          shm_root_dir_,
          socket_ns_,
          channel.get_uuid().to_string(),
          channel_name,
          channel.get_num_slots(),
          callback_fn));
    }
  }
}

} // namespace clockwork::tools
