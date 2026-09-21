// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/abstract_cog_queue.hh"
#include "clockwork/common/cog_envelope.hh"
#include "clockwork/common/tests/support/fake_cog.hh"
#include "clockwork/runners/deterministic_cog_queue.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/testing/expected_stringmakers.hh" // IWYU pragma: keep
#include "jewels/time/sync_time.hh"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_tostring.hpp>

#include <chrono>
#include <memory_resource>
#include <sstream>
#include <string>

namespace Catch
{
template <>
struct StringMaker<::clockwork::CogEnvelope>
{
  static std::string convert(const ::clockwork::CogEnvelope& value)
  {
    std::stringstream oss;
    oss << "CogEnvelope{.ready_time=" << value.ready_time.time_since_epoch().count() << ", .cog=" << value.cog.get()
        << "}";
    return oss.str();
  }
};
} // namespace Catch

namespace clockwork
{
namespace
{

TEST_CASE("execute", "[DeterministicCogQueue]")
{
  using namespace std::chrono_literals;

  constexpr auto start_time = jewels::time::SyncTime(1000ms);

  auto resource = jewels::memory::MemoryResource(std::pmr::new_delete_resource());

  DeterministicCogQueue queue(resource);

  testing::FakeCog cog1(jewels::memory::make_non_null_from_ref(queue));
  testing::FakeCog cog2(jewels::memory::make_non_null_from_ref(queue));
  auto cog1_ptr = jewels::memory::make_non_null_from_ref(cog1);
  auto cog2_ptr = jewels::memory::make_non_null_from_ref(cog2);

  CHECK(queue.stats().size == 0);
  CHECK(!queue.pop(0ns).has_value());

  SECTION("in order")
  {
    queue.push({.ready_time = start_time + 10ms, .cog = cog1_ptr});
    queue.push({.ready_time = start_time + 20ms, .cog = cog2_ptr});
    CHECK(queue.stats().size == 2);
    CHECK(queue.pop(0ns) == CogEnvelope{.ready_time = start_time + 10ms, .cog = cog1_ptr});
    CHECK(queue.pop(0ns) == CogEnvelope{.ready_time = start_time + 20ms, .cog = cog2_ptr});
  }

  SECTION("rev order")
  {
    queue.push({.ready_time = start_time + 30ms, .cog = cog1_ptr});
    queue.push({.ready_time = start_time + 20ms, .cog = cog2_ptr});
    CHECK(queue.stats().size == 2);
    CHECK(queue.pop(0ns) == CogEnvelope{.ready_time = start_time + 20ms, .cog = cog2_ptr});
    CHECK(queue.pop(0ns) == CogEnvelope{.ready_time = start_time + 30ms, .cog = cog1_ptr});
  }

  SECTION("stable order")
  {
    queue.push({.ready_time = start_time + 10ms, .cog = cog1_ptr});
    queue.push({.ready_time = start_time + 10ms, .cog = cog2_ptr});
    CHECK(queue.stats().size == 2);
    CHECK(queue.pop(0ns) == CogEnvelope{.ready_time = start_time + 10ms, .cog = cog2_ptr});
    CHECK(queue.pop(0ns) == CogEnvelope{.ready_time = start_time + 10ms, .cog = cog1_ptr});
  }

  SECTION("duplicate notifications preserve an earlier existing ready time")
  {
    queue.push({.ready_time = start_time + 10ms, .cog = cog1_ptr});
    queue.push({.ready_time = start_time + 20ms, .cog = cog1_ptr});

    CHECK(queue.stats().size == 1);
    CHECK(queue.pop(0ns) == CogEnvelope{.ready_time = start_time + 10ms, .cog = cog1_ptr});
  }

  SECTION("duplicate notifications move a cog to an earlier ready time")
  {
    queue.push({.ready_time = start_time + 20ms, .cog = cog1_ptr});
    queue.push({.ready_time = start_time + 30ms, .cog = cog1_ptr});
    queue.push({.ready_time = start_time + 10ms, .cog = cog1_ptr});

    CHECK(queue.stats().size == 1);
    CHECK(queue.pop(0ns) == CogEnvelope{.ready_time = start_time + 10ms, .cog = cog1_ptr});
  }

  SECTION("throttle deadlines preserve nodes while later Cogs progress")
  {
    queue.push({.ready_time = start_time + 10ms, .cog = cog1_ptr});
    queue.push({.ready_time = start_time + 20ms, .cog = cog2_ptr});
    queue.set_throttled_until(cog1_ptr, start_time + 30ms);

    const auto next = queue.peek();
    REQUIRE(next);
    CHECK(next->cog == cog2_ptr);
    CHECK(next->ready_time == start_time + 20ms);
    CHECK(queue.stats().size == 2);

    queue.remove_next();
    const auto throttled = queue.peek();
    REQUIRE(throttled);
    CHECK(throttled->cog == cog1_ptr);
    CHECK(throttled->ready_time == start_time + 30ms);
    CHECK(queue.stats().size == 1);
    queue.remove_next();
  }

  SECTION("duplicate notifications do not bypass a throttle deadline")
  {
    queue.push({.ready_time = start_time + 10ms, .cog = cog1_ptr});
    queue.set_throttled_until(cog1_ptr, start_time + 30ms);
    queue.push({.ready_time = start_time + 15ms, .cog = cog1_ptr});
    queue.push({.ready_time = start_time + 20ms, .cog = cog2_ptr});

    const auto next = queue.peek();
    REQUIRE(next);
    CHECK(next->cog == cog2_ptr);
    CHECK(next->ready_time == start_time + 20ms);
    CHECK(queue.stats().size == 2);
    CHECK(queue.pop(0ns) == CogEnvelope{.ready_time = start_time + 20ms, .cog = cog2_ptr});

    const auto throttled = queue.peek();
    REQUIRE(throttled);
    CHECK(throttled->cog == cog1_ptr);
    CHECK(throttled->ready_time == start_time + 30ms);
    queue.remove_next();
  }

  CHECK(queue.pop(0ns) == jewels::unexpected(jewels::MonoError{}));
  CHECK(queue.stats().size == 0);
}

} // namespace
} // namespace clockwork
