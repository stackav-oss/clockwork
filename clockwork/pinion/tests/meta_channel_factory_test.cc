// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/pinion/abstract_channel.hh"
#include "clockwork/pinion/abstract_channel_factory.hh"
#include "clockwork/pinion/buffer_layout.hh"
#include "clockwork/pinion/channel_config_clk_cc.hh"
#include "clockwork/pinion/meta_channel_factory.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_test_macros.hpp>
#include <trompeloeil/catch2.hpp> // IWYU pragma: keep (registers catch2 as the error handler)
#include <trompeloeil/mock.hpp>

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <memory_resource>
#include <string>
#include <string_view>
#include <unordered_map>

namespace clockwork::pinion
{
bool operator==(const BufferLayout& lhs, const BufferLayout& rhs)
{
  return lhs.num_slots == rhs.num_slots && lhs.message_size == rhs.message_size &&
         lhs.is_published_once == rhs.is_published_once && lhs.max_msgs_per_exec == rhs.max_msgs_per_exec;
}

namespace
{

class MockChannelFactory : public AbstractChannelFactory
{
public:
  MockChannelFactory() = default;

  MAKE_MOCK4(
    open_publisher,
    (jewels::expected<std::shared_ptr<AbstractPublisher>, AbstractChannel::Error>)(std::string_view,
                                                                                   std::string_view,
                                                                                   const BufferLayout&,
                                                                                   size_t),
    override);

  MAKE_MOCK4(
    open_subscriber,
    (jewels::expected<std::shared_ptr<AbstractSubscriber>, AbstractChannel::Error>)(std::string_view,
                                                                                    std::string_view,
                                                                                    const BufferLayout&,
                                                                                    size_t),
    override);

  MAKE_MOCK4(
    open_spy,
    (jewels::expected<std::shared_ptr<AbstractSubscriber>, AbstractChannel::Error>)(std::string_view,
                                                                                    std::string_view,
                                                                                    const BufferLayout&,
                                                                                    size_t),
    override);
};

TEST_CASE("MetaChannelFactory")
{
  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};
  const auto channel1 = jewels::Uuid<common::MemoryResourceId>::random_uuid();
  const auto channel2 = jewels::Uuid<common::MemoryResourceId>::random_uuid();
  BufferLayout layout{.num_slots = 4, .message_size = 99, .is_published_once = false};

  std::pmr::unordered_map<std::pmr::string, pinion::ChannelType> channel_types;
  channel_types[channel1.to_string(memres)] = ChannelType::unspecified;
  channel_types[channel2.to_string(memres)] = ChannelType::shared_memory;

  auto factory_unspecified = std::make_shared<MockChannelFactory>();
  auto factory_shared_memory = std::make_shared<MockChannelFactory>();
  std::map<ChannelType, std::shared_ptr<pinion::AbstractChannelFactory>> factories;
  factories[ChannelType::unspecified] = factory_unspecified;
  factories[ChannelType::shared_memory] = factory_shared_memory;

  MetaChannelFactory factory{memres, channel_types, factories};

  REQUIRE_CALL(*factory_unspecified, open_publisher(channel1.to_string(), "channel1", layout, 6)).LR_RETURN(nullptr);
  factory.open(AbstractChannel::Role::publisher, channel1.to_string(), "channel1", layout, 6);

  REQUIRE_CALL(*factory_shared_memory, open_subscriber(channel2.to_string(), "channel2", layout, 7)).LR_RETURN(nullptr);
  factory.open(AbstractChannel::Role::subscriber, channel2.to_string(), "channel2", layout, 7);
}

} // namespace
} // namespace clockwork::pinion
