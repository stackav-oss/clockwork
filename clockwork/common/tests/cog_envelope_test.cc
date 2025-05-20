// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/cog_envelope.hh"
#include "clockwork/common/tests/support/test_cog.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/time/sync_time.hh"

#include <catch2/catch_test_macros.hpp>

#include <chrono>

namespace clockwork
{
namespace
{

TEST_CASE("equality", "[CogEnvelope]")
{
  auto queue = NullCogQueue();
  auto cog0 = TestCog(jewels::memory::make_non_null_from_ref(queue));
  auto cog1 = TestCog(jewels::memory::make_non_null_from_ref(queue));

  auto env0 = CogEnvelope{
    .ready_time = jewels::time::SyncTime(std::chrono::seconds{1}),
    .cog = jewels::memory::make_non_null_from_ref(cog0),
  };

  auto env1 = CogEnvelope{
    .ready_time = jewels::time::SyncTime(std::chrono::seconds{2}),
    .cog = jewels::memory::make_non_null_from_ref(cog1),
  };

  auto env2 = CogEnvelope{
    .ready_time = env0.ready_time,
    .cog = env1.cog,
  };

  REQUIRE(env0 == env0);
  REQUIRE(env1 == env1);
  REQUIRE(env2 == env2);
  REQUIRE_FALSE(env0 == env1);
  REQUIRE_FALSE(env0 == env2);
  REQUIRE_FALSE(env1 == env2);

  REQUIRE_FALSE(env0 != env0);
  REQUIRE_FALSE(env1 != env1);
  REQUIRE_FALSE(env2 != env2);
  REQUIRE(env0 != env1);
  REQUIRE(env0 != env2);
  REQUIRE(env1 != env2);
}

} // namespace
} // namespace clockwork
