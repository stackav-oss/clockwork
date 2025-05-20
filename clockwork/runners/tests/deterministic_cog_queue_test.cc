// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/abstract_cog_queue.hh"
#include "clockwork/common/cog_envelope.hh"
#include "clockwork/common/tests/support/fake_cog.hh"
#include "clockwork/runners/deterministic_cog_queue.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
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
template <>
struct StringMaker<::jewels::MonoError>
{
  static std::string convert(const ::jewels::MonoError& /*value*/)
  {
    return "MonoError";
  }
};

template <typename Result, typename Error>
struct StringMaker<::jewels::expected<Result, Error>>
{
  static std::string convert(const ::jewels::expected<Result, Error>& value)
  {
    if (value.has_value())
    {
      return StringMaker<Result>::convert(value.value());
    }
    return StringMaker<Error>::convert(value.error());
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
    queue.push({.ready_time = start_time + 30ms, .cog = cog2_ptr});
    CHECK(queue.stats().size == 3);
    CHECK(queue.pop(0ns) == CogEnvelope{start_time + 10ms, cog1_ptr});
    CHECK(queue.pop(0ns) == CogEnvelope{start_time + 20ms, cog2_ptr});
    CHECK(queue.pop(0ns) == CogEnvelope{start_time + 30ms, cog2_ptr});
  }

  SECTION("rev order")
  {
    queue.push({.ready_time = start_time + 30ms, .cog = cog1_ptr});
    queue.push({.ready_time = start_time + 20ms, .cog = cog2_ptr});
    queue.push({.ready_time = start_time + 10ms, .cog = cog2_ptr});
    CHECK(queue.stats().size == 3);
    CHECK(queue.pop(0ns) == CogEnvelope{start_time + 10ms, cog2_ptr});
    CHECK(queue.pop(0ns) == CogEnvelope{start_time + 20ms, cog2_ptr});
    CHECK(queue.pop(0ns) == CogEnvelope{start_time + 30ms, cog1_ptr});
  }

  SECTION("stable order")
  {
    queue.push({.ready_time = start_time + 10ms, .cog = cog1_ptr});
    queue.push({.ready_time = start_time + 10ms, .cog = cog2_ptr});
    queue.push({.ready_time = start_time + 10ms, .cog = cog2_ptr});
    CHECK(queue.stats().size == 3);
    CHECK(queue.pop(0ns) == CogEnvelope{start_time + 10ms, cog2_ptr});
    CHECK(queue.pop(0ns) == CogEnvelope{start_time + 10ms, cog2_ptr});
    CHECK(queue.pop(0ns) == CogEnvelope{start_time + 10ms, cog1_ptr});
  }

  CHECK(queue.pop(0ns) == jewels::unexpected(jewels::MonoError{}));
  CHECK(queue.stats().size == 0);
}

} // namespace
} // namespace clockwork
