// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/dsl/tests/support/msg_with_au.hh"
#include "clockwork/repr_iface.hh"

#include <au/quantity.hh>
#include <au/units/meters.hh>
#include <catch2/catch_test_macros.hpp>

namespace clockwork
{

using clockwork::testing::MsgWithAu;

TEST_CASE("message_with_aurora_units")
{
  const Tappy<MsgWithAu> msg;
  CHECK(msg.get_distance() == au::meters(22.2));
}

} // namespace clockwork
