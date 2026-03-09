// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/scaffolding/data_source_loader.hh"
#include "clockwork/scaffolding/first_message_cache.hh"
#include "clockwork/tags.hh"
#include "clockwork/test_tools/synthetic_message_fetcher.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/container/compare.hh"
#include "jewels/container/tap/var_array.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory_resource>
#include <string_view>
#include <vector>

namespace clockwork::scaffolding
{

namespace
{

// Simple test message type
struct TestMessage
{
  uint32_t value;
};

TEST_CASE("populate_first_message_cache - no log data sources")
{
  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};

  // Create a process description with no log-based data sources
  Tappy<common::ProcessDescription<>> desc;

  FirstMessageCache cache(memres);

  // Create empty synthetic message fetcher
  testing::SyntheticMessageFetcher message_fetcher(memres);
  REQUIRE(message_fetcher.initialize());

  auto outcome = populate_first_message_cache(jewels::Out{cache}, desc.get_data_sources(), message_fetcher, memres);

  REQUIRE(jewels::ok(outcome));
  CHECK(cache.empty());
}

TEST_CASE("populate_first_message_cache - reads first messages")
{
  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};

  constexpr std::string_view channel1_name = "/test/channel1";
  constexpr std::string_view channel2_name = "/test/channel2";
  constexpr std::string_view channel3_name = "/test/channel3";

  const TestMessage msg1{.value = 111};
  const TestMessage msg2{.value = 222};
  const TestMessage msg3{.value = 333};
  const TestMessage msg4{.value = 444};

  // Create synthetic message fetcher with test messages
  testing::SyntheticMessageFetcher message_fetcher(memres);

  message_fetcher.add_message(msg1, jewels::time::SyncTime{std::chrono::seconds(1)}, channel1_name);
  message_fetcher.add_message(msg3, jewels::time::SyncTime{std::chrono::seconds(2)}, channel2_name);
  message_fetcher.add_message(msg2, jewels::time::SyncTime{std::chrono::seconds(3)}, channel1_name);
  message_fetcher.add_message(msg4, jewels::time::SyncTime{std::chrono::seconds(4)}, channel3_name);

  REQUIRE(message_fetcher.initialize());

  // Create process description with log data sources for channels 1 and 2
  const auto repr_id = jewels::Uuid<RepresentationTag>::random_uuid();

  std::vector<Tappy<common::DataSource<>>> data_sources = {
    TapInit<Tachyon<common::DataSource<4096>>>{
      .representation_id = repr_id,
      .data_source_type = common::DataSourceType::log_first_message,
      .source_path_or_name = jewels::tap::VarString<4096>{""},
      .fallback_source = common::no_fallback_data_source_sentinel},
    TapInit<Tachyon<common::DataSource<4096>>>{
      .representation_id = repr_id,
      .data_source_type = common::DataSourceType::log_first_message,
      .source_path_or_name = jewels::tap::VarString<4096>{""},
      .fallback_source = common::no_fallback_data_source_sentinel}};
  CHECK(data_sources[0].get_underlying_source_path_or_name().try_set(channel1_name));
  CHECK(data_sources[1].get_underlying_source_path_or_name().try_set(channel2_name));

  Tappy<common::ProcessDescription<>> desc;
  for (const auto& data_source : data_sources)
  {
    REQUIRE(desc.get_underlying_data_sources().try_emplace_back(data_source));
  }

  FirstMessageCache cache(memres);
  auto outcome = populate_first_message_cache(jewels::Out{cache}, desc.get_data_sources(), message_fetcher, memres);

  REQUIRE(jewels::ok(outcome));
  REQUIRE(cache.size() == 2);

  // Check that we got the FIRST message from each channel
  REQUIRE(cache.contains(channel1_name));
  const auto& cached_msg1 = cache.at(channel1_name);
  REQUIRE(cached_msg1.size() == sizeof(TestMessage));
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) Needed to verify message contents
  const auto* cached_test_msg1 = reinterpret_cast<const TestMessage*>(cached_msg1.data());
  CHECK(cached_test_msg1->value == msg1.value);

  REQUIRE(cache.contains(channel2_name));
  const auto& cached_msg3 = cache.at(channel2_name);
  REQUIRE(cached_msg3.size() == sizeof(TestMessage));
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) Needed to verify message contents
  const auto* cached_test_msg3 = reinterpret_cast<const TestMessage*>(cached_msg3.data());
  CHECK(cached_test_msg3->value == msg3.value);

  // channel3 was not in the data sources, so shouldn't be in cache
  CHECK_FALSE(cache.contains(channel3_name));
}

TEST_CASE("populate_first_message_cache - handles missing channels")
{
  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};

  constexpr std::string_view existing_channel = "/exists";
  constexpr std::string_view missing_channel = "/missing";

  const TestMessage existing_msg{.value = 42};

  // Create synthetic message fetcher with only one of the channels
  testing::SyntheticMessageFetcher message_fetcher(memres);
  message_fetcher.add_message(existing_msg, jewels::time::SyncTime{std::chrono::seconds(1)}, existing_channel);

  REQUIRE(message_fetcher.initialize());

  const auto repr_id = jewels::Uuid<RepresentationTag>::random_uuid();

  std::vector<Tappy<common::DataSource<>>> data_sources = {
    TapInit<Tachyon<common::DataSource<4096>>>{
      .representation_id = repr_id,
      .data_source_type = common::DataSourceType::log_first_message,
      .source_path_or_name = jewels::tap::VarString<4096>{""},
      .fallback_source = common::no_fallback_data_source_sentinel},
    TapInit<Tachyon<common::DataSource<4096>>>{
      .representation_id = repr_id,
      .data_source_type = common::DataSourceType::log_first_message,
      .source_path_or_name = jewels::tap::VarString<4096>{""},
      .fallback_source = common::no_fallback_data_source_sentinel}};
  CHECK(data_sources[0].get_underlying_source_path_or_name().try_set(existing_channel));
  CHECK(data_sources[1].get_underlying_source_path_or_name().try_set(missing_channel));

  Tappy<common::ProcessDescription<>> desc;
  for (const auto& data_source : data_sources)
  {
    REQUIRE(desc.get_underlying_data_sources().try_emplace_back(data_source));
  }

  FirstMessageCache cache(memres);
  auto outcome = populate_first_message_cache(jewels::Out{cache}, desc.get_data_sources(), message_fetcher, memres);

  // Should still succeed, but only find one channel
  REQUIRE(jewels::ok(outcome));
  REQUIRE(cache.size() == 1);
  REQUIRE(cache.contains(existing_channel));
  CHECK_FALSE(cache.contains(missing_channel));
}

} // namespace

} // namespace clockwork::scaffolding
