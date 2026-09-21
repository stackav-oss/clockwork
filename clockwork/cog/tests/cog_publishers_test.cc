// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/cog/cog_publishers.hh"
#include "clockwork/cog/detail.hh"
#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/in_memory_channel.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/slot_ref.hh"
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
#include <variant>

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
  return std::tuple<Types...>(Types{args...}...);
}

template <typename... Policies>
struct CogPublishersFixture // NOLINT(clang-analyzer-optin.performance.Padding) Test code so performance is not a
                            // concern.
{
  static constexpr auto policy_count = sizeof...(Policies);
  static constexpr auto channel_size = 100;
  using ChannelsTuple =
    std::tuple<std::shared_ptr<InMemoryChannel<typename Policies::MsgType, channel_size, false>>...>;

  CogPublishersFixture()
    : resource(std::pmr::new_delete_resource()),
      channels(
        std::make_tuple(
          std::make_shared<InMemoryChannel<typename Policies::MsgType, channel_size, false>>(resource)...)),
      publisher(resource)
  {
  }

  jewels::memory::MemoryResource resource;
  ChannelsTuple channels;
  CogPublishers<Policies...> publisher;
};

struct PublisherPolicy1
{
  using MsgType = TestMsg1;
  static constexpr auto endpoint_id =
    jewels::Uuid<common::EndpointClassId>::from_string("b6e2b628-62ba-4c73-b07e-b2ce77a742b4").value();
  static constexpr std::string_view name = "PublisherPolicy1";
  static constexpr std::optional<clockwork::RateLimitParameters> rate_limit_params{};
  static constexpr size_t max_msgs_per_exec = 1U;
};

struct PublisherPolicy2
{
  using MsgType = TestMsg2;
  static constexpr auto endpoint_id =
    jewels::Uuid<common::EndpointClassId>::from_string("789e340c-556f-4cf0-a3b9-73632ba75a7c").value();
  static constexpr std::string_view name = "PublisherPolicy2";
  static constexpr std::optional<clockwork::RateLimitParameters> rate_limit_params{};
  static constexpr size_t max_msgs_per_exec = 1U;
};

struct MultiPublisherPolicy1
{
  using MsgType = TestMsg1;
  static constexpr auto endpoint_id =
    jewels::Uuid<common::EndpointClassId>::from_string("b6e2b628-62ba-4c73-b07e-b2ce77a742b4").value();
  static constexpr std::string_view name = "MultiPublisherPolicy1";
  static constexpr std::optional<clockwork::RateLimitParameters> rate_limit_params{};
  static constexpr size_t max_msgs_per_exec = 2U;
};

using PublisherPolicyFixture = CogPublishersFixture<PublisherPolicy1, PublisherPolicy2>;
using MultiPublisherPolicyFixture = CogPublishersFixture<MultiPublisherPolicy1, PublisherPolicy2>;

TEST_CASE_METHOD(PublisherPolicyFixture, "make_publishables", "[publisher]")
{
  REQUIRE(publisher.set_handle(PublisherPolicy1::endpoint_id, std::get<0>(channels)->make_publisher(1)));
  REQUIRE(publisher.set_handle(PublisherPolicy2::endpoint_id, std::get<1>(channels)->make_publisher(1)));
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
    auto available = std::get<0>(channels)->available();
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
    auto available = std::get<1>(channels)->available();
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

TEST_CASE_METHOD(
  MultiPublisherPolicyFixture, "single endpoint can publish multiple messages in one execution cycle", "[publisher]")
{
  REQUIRE(publisher.set_handle(MultiPublisherPolicy1::endpoint_id, std::get<0>(channels)->make_publisher(1)));
  REQUIRE(publisher.set_handle(PublisherPolicy2::endpoint_id, std::get<1>(channels)->make_publisher(1)));
  REQUIRE(publisher.validate());

  {
    auto slots = publisher.reserve_slots();
    REQUIRE(slots);

    auto publishables = publisher.make_publishables(*slots);
    REQUIRE(publishables);

    auto& output = std::get<0>(*publishables);

    output.message(0).value = 11;
    output.message(1).value = 22;
    output.mark_for_publish(2);

    REQUIRE(std::get<0>(*slots).commit(fake_publish_time));
  }

  auto available = std::get<0>(channels)->available();
  auto range = pinion::to_message_range<const TestMsg1>(available);
  REQUIRE(range);

  REQUIRE(range->size() == 2U);
}

TEST_CASE_METHOD(
  MultiPublisherPolicyFixture, "partial publish commits only the requested number of messages", "[publisher]")
{
  REQUIRE(publisher.set_handle(MultiPublisherPolicy1::endpoint_id, std::get<0>(channels)->make_publisher(1)));
  REQUIRE(publisher.set_handle(PublisherPolicy2::endpoint_id, std::get<1>(channels)->make_publisher(1)));
  REQUIRE(publisher.validate());

  {
    auto slots = publisher.reserve_slots();
    REQUIRE(slots);

    auto publishables = publisher.make_publishables(*slots);
    REQUIRE(publishables);

    auto& output = std::get<0>(*publishables);

    output.message(0).value = 11;
    output.message(1).value = 22;
    output.mark_for_publish(1);

    REQUIRE(std::get<0>(*slots).process(fake_publish_time));
  }

  auto available = std::get<0>(channels)->available();
  auto range = pinion::to_message_range<const TestMsg1>(available);
  REQUIRE(range);
  REQUIRE(range->size() == 1U);
  REQUIRE(range->front().value == 11);
}

TEST_CASE_METHOD(MultiPublisherPolicyFixture, "metrics track marked output messages", "[publisher]")
{
  REQUIRE(publisher.set_handle(MultiPublisherPolicy1::endpoint_id, std::get<0>(channels)->make_publisher(1)));
  REQUIRE(publisher.set_handle(PublisherPolicy2::endpoint_id, std::get<1>(channels)->make_publisher(1)));
  REQUIRE(publisher.validate());

  {
    auto slots = publisher.reserve_slots();
    REQUIRE(slots);

    auto publishables = publisher.make_publishables(*slots);
    REQUIRE(publishables);

    auto& output = std::get<0>(*publishables);
    CHECK(output.get_metrics_publish_count() == 0U);
    CHECK_FALSE(output.get_metrics_first_sequence_number());

    output.mark_for_publish(2);
    CHECK(output.get_metrics_publish_count() == 2U);
    REQUIRE(output.get_metrics_first_sequence_number());
    CHECK(*output.get_metrics_first_sequence_number() == 0U);

    REQUIRE(std::get<0>(*slots).process(fake_publish_time));
  }

  {
    auto slots = publisher.reserve_slots();
    REQUIRE(slots);

    auto publishables = publisher.make_publishables(*slots);
    REQUIRE(publishables);

    auto& output = std::get<0>(*publishables);
    output.mark_for_publish(1);
    CHECK(output.get_metrics_publish_count() == 1U);
    REQUIRE(output.get_metrics_first_sequence_number());
    CHECK(*output.get_metrics_first_sequence_number() == 2U);

    REQUIRE(std::get<0>(*slots).process(fake_publish_time));
  }
}

TEST_CASE_METHOD(MultiPublisherPolicyFixture, "mark_for_publish(0) publishes no messages", "[publisher]")
{
  REQUIRE(publisher.set_handle(MultiPublisherPolicy1::endpoint_id, std::get<0>(channels)->make_publisher(1)));
  REQUIRE(publisher.set_handle(PublisherPolicy2::endpoint_id, std::get<1>(channels)->make_publisher(1)));
  REQUIRE(publisher.validate());

  {
    auto slots = publisher.reserve_slots();
    REQUIRE(slots);

    auto publishables = publisher.make_publishables(*slots);
    REQUIRE(publishables);

    auto& output = std::get<0>(*publishables);

    output.message(0).value = 99;
    output.mark_for_publish(0);
    REQUIRE_FALSE(output.is_marked_for_publish());

    REQUIRE(std::get<0>(*slots).process(fake_publish_time));
  }

  auto available = std::get<0>(channels)->available();
  REQUIRE(available.empty());
}

TEST_CASE_METHOD(MultiPublisherPolicyFixture, "repeated mark_for_publish with same count is idempotent", "[publisher]")
{
  REQUIRE(publisher.set_handle(MultiPublisherPolicy1::endpoint_id, std::get<0>(channels)->make_publisher(1)));
  REQUIRE(publisher.set_handle(PublisherPolicy2::endpoint_id, std::get<1>(channels)->make_publisher(1)));
  REQUIRE(publisher.validate());

  {
    auto slots = publisher.reserve_slots();
    REQUIRE(slots);

    auto publishables = publisher.make_publishables(*slots);
    REQUIRE(publishables);

    auto& output = std::get<0>(*publishables);

    output.message(0).value = 11;
    output.message(1).value = 22;
    output.mark_for_publish(1);
    output.mark_for_publish(1); // same value: no error, same result

    REQUIRE(std::get<0>(*slots).process(fake_publish_time));
  }

  auto available = std::get<0>(channels)->available();
  auto range = pinion::to_message_range<const TestMsg1>(available);
  REQUIRE(range);
  REQUIRE(range->size() == 1U);
  REQUIRE(range->front().value == 11);
}

TEST_CASE_METHOD(MultiPublisherPolicyFixture, "repeated mark_for_publish uses largest count", "[publisher]")
{
  REQUIRE(publisher.set_handle(MultiPublisherPolicy1::endpoint_id, std::get<0>(channels)->make_publisher(1)));
  REQUIRE(publisher.set_handle(PublisherPolicy2::endpoint_id, std::get<1>(channels)->make_publisher(1)));
  REQUIRE(publisher.validate());

  {
    auto slots = publisher.reserve_slots();
    REQUIRE(slots);

    auto publishables = publisher.make_publishables(*slots);
    REQUIRE(publishables);

    auto& output = std::get<0>(*publishables);

    output.message(0).value = 11;
    output.message(1).value = 22;
    // First call: publish 1; second call: publish 2
    // largest wins
    output.mark_for_publish(1);
    output.mark_for_publish(2);

    REQUIRE(std::get<0>(*slots).process(fake_publish_time));
  }

  auto available = std::get<0>(channels)->available();
  auto range = pinion::to_message_range<const TestMsg1>(available);
  REQUIRE(range);
  REQUIRE(range->size() == 2U);
  CHECK(range->front().value == 11);
  CHECK(range->back().value == 22);
}

TEST_CASE_METHOD(PublisherPolicyFixture, "make connected and not connected publishables", "[publisher]")
{
  REQUIRE(publisher.set_handle(PublisherPolicy1::endpoint_id, std::get<0>(channels)->make_publisher(1), false));
  REQUIRE(publisher.set_handle(PublisherPolicy2::endpoint_id, std::get<1>(channels)->make_publisher(1), true));
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

TEST_CASE_METHOD(
  MultiPublisherPolicyFixture, "multi-message publisher not connected propagates disconnected flag", "[publisher]")
{
  // connected = false: reservation succeeds but publishable reports not connected
  REQUIRE(publisher.set_handle(MultiPublisherPolicy1::endpoint_id, std::get<0>(channels)->make_publisher(1), false));
  REQUIRE(publisher.set_handle(PublisherPolicy2::endpoint_id, std::get<1>(channels)->make_publisher(1), true));
  REQUIRE(publisher.validate());

  auto slots = publisher.reserve_slots();
  REQUIRE(slots);

  auto publishables = publisher.make_publishables(*slots);
  REQUIRE(publishables);

  auto& output = std::get<0>(*publishables);
  REQUIRE_FALSE(output.connected());

  auto& output2 = std::get<1>(*publishables);
  REQUIRE(output2.connected());
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
  static constexpr size_t max_msgs_per_exec = 1U;
};

struct LimitedPublisherPolicy
{
  using MsgType = TestMsg2;
  static constexpr auto endpoint_id =
    jewels::Uuid<common::EndpointClassId>::from_string("789e340c-556f-4cf0-a3b9-73632ba75a7c").value();
  static constexpr std::string_view name = "PublisherPolicy2";
  static constexpr std::optional<clockwork::RateLimitParameters> rate_limit_params{
    {.limit = 1U, .period = std::chrono::seconds{1}}};
  static constexpr size_t max_msgs_per_exec = 1U;
};

struct LimitedPublisherPolicy2
{
  using MsgType = TestMsg2;
  static constexpr auto endpoint_id =
    jewels::Uuid<common::EndpointClassId>::from_string("ae548e3b-eca7-489d-a3d0-5d36cb8c5df7").value();
  static constexpr std::string_view name = "PublisherPolicy3";
  static constexpr std::optional<clockwork::RateLimitParameters> rate_limit_params{
    {.limit = 2U, .period = std::chrono::seconds{1}}};
  static constexpr size_t max_msgs_per_exec = 1U;
};

using RateLimitPublisherPolicyFixture =
  CogPublishersFixture<UnlimitedPublisherPolicy, LimitedPublisherPolicy, LimitedPublisherPolicy2>;

TEST_CASE_METHOD(RateLimitPublisherPolicyFixture, "rate limit state", "[publisher]")
{
  REQUIRE(publisher.set_handle(UnlimitedPublisherPolicy::endpoint_id, std::get<0>(channels)->make_publisher(1)));
  REQUIRE(publisher.set_handle(LimitedPublisherPolicy::endpoint_id, std::get<1>(channels)->make_publisher(1)));
  REQUIRE(publisher.set_handle(LimitedPublisherPolicy2::endpoint_id, std::get<2>(channels)->make_publisher(1)));
  REQUIRE(publisher.validate());

  // We should be able to publish on the unlimited output without getting
  // throttled.
  jewels::time::SyncTime now{std::chrono::seconds{0}};
  auto throttled_until = jewels::time::SyncTime::min();
  decltype(publisher)::PublisherThrottleSet throttled_publishers;
  REQUIRE(
    jewels::ok(publisher.update_rate_limiters(jewels::Out{throttled_until}, jewels::Out{throttled_publishers}, now)));
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
    REQUIRE(
      jewels::ok(publisher.update_rate_limiters(jewels::Out{throttled_until}, jewels::Out{throttled_publishers}, now)));
  }

  // But, if we exceed the limit on the limited publisher, the whole cog should get throttled.
  now += std::chrono::milliseconds{1};
  REQUIRE(
    jewels::ok(publisher.update_rate_limiters(jewels::Out{throttled_until}, jewels::Out{throttled_publishers}, now)));
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
  REQUIRE(
    jewels::fails(
      publisher.update_rate_limiters(jewels::Out{throttled_until}, jewels::Out{throttled_publishers}, now)));
  REQUIRE(throttled_until == jewels::time::SyncTime{std::chrono::seconds{1}});
  REQUIRE(throttled_publishers == decltype(throttled_publishers){0b010U});

  // If we wait long enough, we should be clear to execute.
  now += std::chrono::milliseconds{999};
  REQUIRE(
    jewels::ok(publisher.update_rate_limiters(jewels::Out{throttled_until}, jewels::Out{throttled_publishers}, now)));

  // We should also get throttled if the rate of other limited publisher is
  // exceeded.  However, an entire limit window has elapsed since the first
  // token was issued for that publisher and it has accumulated a new one.
  for (auto i = 0; i < 3; i++)
  {
    REQUIRE(
      jewels::ok(publisher.update_rate_limiters(jewels::Out{throttled_until}, jewels::Out{throttled_publishers}, now)));
    auto slots = publisher.reserve_slots();
    REQUIRE(slots);
    auto publishables = publisher.make_publishables(*slots);
    REQUIRE(publishables);
    std::get<2>(*publishables).mark_for_publish();
    publisher.update_throttle_status(*slots);
    REQUIRE(std::get<2>(*slots).commit(fake_publish_time));
    now += std::chrono::milliseconds{1};
  }
  REQUIRE(
    jewels::fails(
      publisher.update_rate_limiters(jewels::Out{throttled_until}, jewels::Out{throttled_publishers}, now)));
  REQUIRE(throttled_publishers == decltype(throttled_publishers){0b100U});
}

} // namespace
} // namespace clockwork
