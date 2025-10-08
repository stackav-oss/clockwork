// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/dial/msg_input.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "clockwork/pinion/tests/support/optionally_connected_cog_dial.hh"

#include <catch2/catch_test_macros.hpp>

namespace clockwork::pinion::test
{
void execute_cog(OptionallyConnectedCogDial& dial)
{
  CHECK(dial.get_outputs().get_connected_optional_output().connected());
  CHECK(dial.get_inputs().get_connected_optional_input().connected());

  CHECK(!dial.get_outputs().get_not_connected_output().connected());
  CHECK(!dial.get_inputs().get_not_connected_input().connected());
  CHECK(dial.get_inputs().get_not_connected_input().get_view().empty());

  CHECK(dial.get_inputs().get_required_connected_input().connected());
  CHECK(dial.get_outputs().get_required_connected_output().connected());
}
} // namespace clockwork::pinion::test
