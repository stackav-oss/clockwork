// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/cog/cog_publishers.hh"
#include "clockwork/common/process_description.hh"
#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/in_memory_channel.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "jewels/container/compare.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_test_macros.hpp>
#include <gsl/util>

#include <array>
#include <chrono>
#include <cstdint>
#include <memory_resource>
#include <ranges>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace clockwork
{
namespace
{

/// Fixed timestamp for publish timestamp.
inline constexpr auto fake_publish_time{jewels::time::SyncTime{std::chrono::nanoseconds{12345}}};

struct TestMsg1
{
  int32_t value = {};
};

struct TestMsg2
{
  int32_t value = {};
};

bool operator==(const TestMsg1& lhs, const TestMsg1& rhs)
{
  return (lhs.value == rhs.value);
}

bool operator==(const TestMsg2& lhs, const TestMsg2& rhs)
{
  return (lhs.value == rhs.value);
}

template <typename... Types, typename... Args>
auto make_tuple_repeat(Args&... args)
{
  // NOLINTNEXTLINE(bugprone-use-after-move) TODO(DX-1794): Fix this
  return std::tuple<Types...>(Types{std::forward<Args...>(args...)}...);
}

template <typename... Policies>
struct CogPublishersFixture // NOLINT(clang-analyzer-optin.performance.Padding) Test code so performance is not a
                            // concern.
{
  static constexpr auto policy_count = sizeof...(Policies);
  static constexpr auto channel_size = 100;
  using ChannelsTuple = std::tuple<InMemoryChannel<typename Policies::MsgType, channel_size>...>;
  using SubscribersArray = std::array<pinion::SubscriberHandle, policy_count>;

  CogPublishersFixture()
    : resource(std::pmr::new_delete_resource()),
      channels(make_tuple_repeat<InMemoryChannel<typename Policies::MsgType, channel_size>...>(resource)),
      subscribers(std::apply([](auto&... chls) -> SubscribersArray { return {chls.make_subscriber()...}; }, channels)),
      publisher(resource)
  {
  }

  jewels::memory::MemoryResource resource;
  ChannelsTuple channels;
  SubscribersArray subscribers;
  CogPublishers<Policies...> publisher;
};

struct PublisherPolicy1
{
  using MsgType = TestMsg1;
  static constexpr auto endpoint_id =
    jewels::Uuid<common::EndpointClassId>::from_string("b6e2b628-62ba-4c73-b07e-b2ce77a742b4").value();
  static constexpr std::string_view name = "PublisherPolicy1";
  static constexpr std::optional<clockwork::RateLimitParameters> rate_limit_params{};
};

struct PublisherPolicy2
{
  using MsgType = TestMsg2;
  static constexpr auto endpoint_id =
    jewels::Uuid<common::EndpointClassId>::from_string("789e340c-556f-4cf0-a3b9-73632ba75a7c").value();
  static constexpr std::string_view name = "PublisherPolicy2";
  static constexpr std::optional<clockwork::RateLimitParameters> rate_limit_params{};
};

using PublisherPolicyFixture = CogPublishersFixture<PublisherPolicy1, PublisherPolicy2>;

TEST_CASE_METHOD(PublisherPolicyFixture, "make_publishables", "[publisher]")
{
  REQUIRE(publisher.set_handle(PublisherPolicy1::endpoint_id, std::get<0>(channels).make_publisher(1)));
  REQUIRE(publisher.set_handle(PublisherPolicy2::endpoint_id, std::get<1>(channels).make_publisher(1)));
  REQUIRE(publisher.validate());

  {
    auto slots = publisher.reserve_slots();
    REQUIRE(slots);

    auto publishables = publisher.make_publishables(*slots);
    REQUIRE(publishables);

    auto publishable1 = std::get<0>(*publishables);
    auto& msg1 = publishable1.message();
    msg1.value = 1;

    auto publishable2 = std::get<1>(*publishables);
    auto& msg2 = publishable2.message();
    msg2.value = 2;

    REQUIRE(std::get<0>(*slots).commit(fake_publish_time));
    REQUIRE(std::get<1>(*slots).commit(fake_publish_time));
  }

  {
    auto available = subscribers.at(0).available();
    REQUIRE(1 == available.size());

    auto range = pinion::to_message_range<const TestMsg1>(available);
    REQUIRE(range);
    REQUIRE(1 == range->size());
    const auto& actual = *range->begin();

    const auto expected = TestMsg1{
      .value = 1,
    };
    REQUIRE(expected == actual);
  }

  {
    auto available = subscribers.at(1).available();
    REQUIRE(1 == available.size());

    auto range = pinion::to_message_range<const TestMsg2>(available);
    REQUIRE(range);
    REQUIRE(1 == range->size());
    const auto& actual = *range->begin();

    const auto expected = TestMsg2{
      .value = 2,
    };
    REQUIRE(expected == actual);
  }
}

TEST_CASE_METHOD(PublisherPolicyFixture, "make connected and not connected publishables", "[publisher]")
{
  REQUIRE(publisher.set_handle(PublisherPolicy1::endpoint_id, std::get<0>(channels).make_publisher(1), false));
  REQUIRE(publisher.set_handle(PublisherPolicy2::endpoint_id, std::get<1>(channels).make_publisher(1), true));
  REQUIRE(publisher.validate());
  // The third publisher has the same endpoint ID as the first but has not been
  // connected.
  REQUIRE(publisher.validate());

  auto slots = publisher.reserve_slots();
  REQUIRE(slots);

  auto publishables = publisher.make_publishables(*slots);
  REQUIRE(publishables);

  auto publishable1 = std::get<0>(*publishables);
  REQUIRE_FALSE(publishable1.connected());

  auto publishable2 = std::get<1>(*publishables);
  REQUIRE(publishable2.connected());
}
using ZeroPublishersPolicyFixture = CogPublishersFixture<>;

TEST_CASE_METHOD(ZeroPublishersPolicyFixture, "zero publishers", "[publisher]")
{
  REQUIRE(publisher.validate());

  auto slots = publisher.reserve_slots();
  REQUIRE(slots);
  REQUIRE(slots->empty());

  auto publishables = publisher.make_publishables(*slots);
  REQUIRE(publishables);
  REQUIRE(0 == std::tuple_size<std::decay_t<decltype(*publishables)>>());
  REQUIRE(
    std::apply(
      [](auto&... each_slot) { return (static_cast<bool>(each_slot.commit(fake_publish_time)) && ...); }, *slots));
}

struct UnlimitedPublisherPolicy
{
  using MsgType = TestMsg1;
  static constexpr auto endpoint_id =
    jewels::Uuid<common::EndpointClassId>::from_string("b6e2b628-62ba-4c73-b07e-b2ce77a742b4").value();
  static constexpr std::string_view name = "PublisherPolicy1";
  static constexpr std::optional<clockwork::RateLimitParameters> rate_limit_params{};
};

struct LimitedPublisherPolicy
{
  using MsgType = TestMsg2;
  static constexpr auto endpoint_id =
    jewels::Uuid<common::EndpointClassId>::from_string("789e340c-556f-4cf0-a3b9-73632ba75a7c").value();
  static constexpr std::string_view name = "PublisherPolicy2";
  static constexpr std::optional<clockwork::RateLimitParameters> rate_limit_params{
    {.limit = 1U, .period = std::chrono::seconds{1}}};
};

struct LimitedPublisherPolicy2
{
  using MsgType = TestMsg2;
  static constexpr auto endpoint_id =
    jewels::Uuid<common::EndpointClassId>::from_string("ae548e3b-eca7-489d-a3d0-5d36cb8c5df7").value();
  static constexpr std::string_view name = "PublisherPolicy3";
  static constexpr std::optional<clockwork::RateLimitParameters> rate_limit_params{
    {.limit = 2U, .period = std::chrono::seconds{1}}};
};

using RateLimitPublisherPolicyFixture =
  CogPublishersFixture<UnlimitedPublisherPolicy, LimitedPublisherPolicy, LimitedPublisherPolicy2>;

TEST_CASE_METHOD(RateLimitPublisherPolicyFixture, "rate limit state", "[publisher]")
{
  REQUIRE(publisher.set_handle(UnlimitedPublisherPolicy::endpoint_id, std::get<0>(channels).make_publisher(1)));
  REQUIRE(publisher.set_handle(LimitedPublisherPolicy::endpoint_id, std::get<1>(channels).make_publisher(1)));
  REQUIRE(publisher.set_handle(LimitedPublisherPolicy2::endpoint_id, std::get<2>(channels).make_publisher(1)));
  REQUIRE(publisher.validate());

  // We should be able to publish on the unlimited output without getting
  // throttled.
  jewels::time::SyncTime now{std::chrono::seconds{0}};
  publisher.update_rate_limiters(now);
  REQUIRE_FALSE(publisher.any_throttled());
  {
    auto slots = publisher.reserve_slots();
    REQUIRE(slots);
    auto publishables = publisher.make_publishables(*slots);
    REQUIRE(publishables);
    std::get<0>(*publishables).mark_for_publish();
    publisher.update_throttle_status(*slots);
    REQUIRE(std::get<0>(*slots).commit(fake_publish_time));
  }
  for (auto i = 0; i < 10; i++)
  {
    now += std::chrono::nanoseconds{1};
    publisher.update_rate_limiters(now);
    REQUIRE_FALSE(publisher.any_throttled());
  }

  // But, if we exceed the limit on the limited publisher, the whole cog should get throttled.
  now += std::chrono::milliseconds{1};
  publisher.update_rate_limiters(now);
  REQUIRE_FALSE(publisher.any_throttled());
  {
    auto slots = publisher.reserve_slots();
    REQUIRE(slots);
    auto publishables = publisher.make_publishables(*slots);
    REQUIRE(publishables);
    std::get<1>(*publishables).mark_for_publish();
    publisher.update_throttle_status(*slots);
    REQUIRE(std::get<1>(*slots).commit(fake_publish_time));
  }
  now += std::chrono::milliseconds{1};
  publisher.update_rate_limiters(now);
  REQUIRE(publisher.any_throttled());

  // If we wait long enough, we should be clear to execute.
  now += std::chrono::milliseconds{999};
  publisher.update_rate_limiters(now);
  REQUIRE_FALSE(publisher.any_throttled());

  // We should also get throttled if the rate of other limited publisher is
  // exceeded.  However, an entire limit window has elapsed since the first
  // token was issued for that publisher and it has accumulated a new one.
  for (auto i = 0; i < 3; i++)
  {
    publisher.update_rate_limiters(now);
    REQUIRE_FALSE(publisher.any_throttled());
    auto slots = publisher.reserve_slots();
    REQUIRE(slots);
    auto publishables = publisher.make_publishables(*slots);
    REQUIRE(publishables);
    std::get<2>(*publishables).mark_for_publish();
    publisher.update_throttle_status(*slots);
    REQUIRE(std::get<2>(*slots).commit(fake_publish_time));
    now += std::chrono::milliseconds{1};
  }
  REQUIRE(publisher.any_throttled());
}

} // namespace
} // namespace clockwork
