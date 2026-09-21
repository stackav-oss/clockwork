// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/cog/cog_passthrough_observer.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/time/sync_time.hh"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>

namespace clockwork
{
namespace
{

class TestCog
{
public:
  void notify(jewels::time::SyncTime /*current_time*/)
  {
    ++count;
  }
  size_t count = {};
};

TEST_CASE("notify", "[CogPassthroughObserver]")
{
  auto cog = TestCog();
  auto observer = CogPassthroughObserver<TestCog>(jewels::memory::make_non_null_from_ref(cog));

  REQUIRE(0 == cog.count);
  observer.notify({});
  REQUIRE(1 == cog.count);
}

} // namespace
} // namespace clockwork
