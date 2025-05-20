// IWYU pragma: private, include "clockwork/tools/channel_spy/tests/support/test_support.hh"
#pragma once
#include "clockwork/tools/channel_spy/tests/support/test_support.hh"

#include "clockwork/common/process_description.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/tools/channel_spy/channel_spy_config.hh"
#include "jewels/container/tap/var_array.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/uuid/uuid.hh"

#include <memory>
#include <span>
#include <stdexcept>

namespace clockwork::tools::tests::support
{

/// Generate valid test channel spy configuration for testing subscriptions
/// @tparam<MessageType> Test message type
/// @return Generated configuration
template <typename MessageType>
  requires TappyType<MessageType>
[[nodiscard]] std::unique_ptr<ChannelSpyConfigTap> gen_channel_spy_config()
{
  auto config = std::make_unique<ChannelSpyConfigTap>();

  auto& channel1 = config->get_underlying_channels().emplace_back();
  channel1.set_uuid(jewels::Uuid<common::EndpointInstanceId>::random_uuid());
  channel1.set_num_slots(2U);
  channel1.set_message_size(sizeof(MessageType));
  channel1.get_underlying_channel_name().set_truncate(test_channel_name1);
  channel1.get_underlying_schema_name().set_truncate(clockwork::LoggingTraits<MessageType>::schema_name);
  if (!channel1.get_underlying_schema_definition().try_set(
        std::as_bytes(std::span{clockwork::LoggingTraits<MessageType>::schema_definition})))
  {
    throw std::runtime_error("Failed to set schema definition");
  }

  auto& channel2a = config->get_underlying_channels().emplace_back();
  channel2a.set_uuid(jewels::Uuid<common::EndpointInstanceId>::random_uuid());
  channel2a.set_num_slots(2U);
  channel2a.set_message_size(sizeof(MessageType));
  channel2a.get_underlying_channel_name().set_truncate(test_channel_name2);
  channel2a.get_underlying_schema_name().set_truncate(clockwork::LoggingTraits<MessageType>::schema_name);
  if (!channel2a.get_underlying_schema_definition().try_set(
        std::as_bytes(std::span{clockwork::LoggingTraits<MessageType>::schema_definition})))
  {
    throw std::runtime_error("Failed to set schema definition");
  }

  auto& channel2b = config->get_underlying_channels().emplace_back();
  channel2b.set_uuid(jewels::Uuid<common::EndpointInstanceId>::random_uuid());
  channel2b.set_num_slots(2U);
  channel2b.set_message_size(sizeof(MessageType));
  channel2b.get_underlying_channel_name().set_truncate(test_channel_name2);
  channel2b.get_underlying_schema_name().set_truncate(clockwork::LoggingTraits<MessageType>::schema_name);
  if (!channel2b.get_underlying_schema_definition().try_set(
        std::as_bytes(std::span{clockwork::LoggingTraits<MessageType>::schema_definition})))
  {
    throw std::runtime_error("Failed to set schema definition");
  }

  return config;
}

} // namespace clockwork::tools::tests::support
