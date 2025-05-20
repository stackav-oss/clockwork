// IWYU pragma: private, include "clockwork/tools/channel_spy/tests/support/test_helper.hh"
#pragma once
#include "clockwork/tools/channel_spy/tests/support/test_helper.hh"

#include "clockwork/repr_iface.hh"
#include "clockwork/tools/channel_spy/channel_spy_config.hh"
#include "clockwork/tools/channel_spy/tests/support/test_publisher.hh"
#include "clockwork/tools/channel_spy/tests/support/test_support.hh"
#include "jewels/uuid/uuid.hh"

#include <cstddef>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace clockwork::tools::tests::support
{

template <typename MessageType>
  requires TappyType<MessageType>
TestHelper<MessageType>::TestHelper(
  std::string_view pinion_shm_root, std::string_view socket_ns, std::unique_ptr<ChannelSpyConfigTap> spy_config)
  : pinion_shm_root_(pinion_shm_root), socket_ns_(socket_ns), spy_config_(std::move(spy_config))
{
}

template <typename MessageType>
  requires TappyType<MessageType>
std::shared_ptr<TestHelper<MessageType>>
TestHelper<MessageType>::make_test_helper(std::string_view pinion_shm_root, std::string_view socket_ns)
{
  auto config = gen_channel_spy_config<MessageType>();
  write_channel_spy_config_file(pinion_shm_root, socket_ns, *config);
  TestHelper<MessageType> test_helper{pinion_shm_root, socket_ns, std::move(config)};
  return std::make_shared<TestHelper<MessageType>>(std::move(test_helper));
}

template <typename MessageType>
  requires TappyType<MessageType>
[[nodiscard]] const ChannelSpyConfigTap& TestHelper<MessageType>::spy_config() const
{
  return *spy_config_;
}

template <typename MessageType>
  requires TappyType<MessageType>
[[nodiscard]] std::string_view TestHelper<MessageType>::channel_name(size_t channel_index) const
{
  if (channel_index > spy_config_->get_channels().size())
  {
    throw std::runtime_error("Invalid channel index");
  }
  return spy_config_->get_channels()[channel_index].get_channel_name();
}

template <typename MessageType>
  requires TappyType<MessageType>
[[nodiscard]] std::shared_ptr<TestPublisher<MessageType>>
TestHelper<MessageType>::open_publisher(size_t channel_index) const
{
  if (channel_index > spy_config_->get_channels().size())
  {
    throw std::runtime_error("Invalid channel index");
  }
  return TestPublisher<MessageType>::open(
    pinion_shm_root_,
    socket_ns_,
    spy_config_->get_channels()[channel_index].get_uuid().to_string(),
    spy_config_->get_channels()[channel_index].get_num_slots());
}

} // namespace clockwork::tools::tests::support
