// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/dsl/tests/support/hellocog.hh"
#include "clockwork/runners/online_cog_queue.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_test_macros.hpp>

#include <memory_resource>

namespace clockwork
{
namespace
{

TEST_CASE("ctor", "[hellocog]")
{
  auto resource = jewels::memory::MemoryResource(std::pmr::new_delete_resource());
  auto queue = clockwork::OnlineCogQueue(resource);
  REQUIRE_NOTHROW(
    clockwork::testing::cogs::HelloCogFactory{}.make(
      resource, jewels::Uuid<clockwork::common::CogInstanceId>{}, jewels::memory::make_non_null_from_ref(queue)));
}

} // namespace
} // namespace clockwork
