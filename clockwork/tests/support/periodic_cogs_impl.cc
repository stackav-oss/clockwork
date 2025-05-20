// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/dial/msg_input.hh"
#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/tests/support/exec_time.hh"
#include "clockwork/tests/support/periodic_cogs_dial.hh"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <ranges>

namespace clockwork::testing
{

namespace
{
template <typename T>
void exe(T& dial)
{
  dial.get_outputs().get_exec_time().message().set_exec_time(dial.get_start_time());
  dial.get_outputs().get_exec_time().mark_for_publish();
}
} // namespace

void execute_cog(Periodic1HzCogDial& dial)
{
  exe(dial);
}

void execute_cog(Periodic10HzCogDial& dial)
{
  exe(dial);
}

void execute_cog(Periodic100HzCogDial& dial)
{
  exe(dial);
}

void execute_cog(Periodic1000HzCogDial& dial)
{
  exe(dial);
}

void execute_cog(PeriodicTesterCogDial& dial)
{
  auto& state = dial.get_states().get_state();
  auto& inputs = dial.get_inputs();

  auto view_1hz = inputs.get_input_1hz().get_new_msgs_view();
  auto view_10hz = inputs.get_input_10hz().get_new_msgs_view();
  auto view_100hz = inputs.get_input_100hz().get_new_msgs_view();
  auto view_1000hz = inputs.get_input_1000hz().get_new_msgs_view();

  // Check that the first execution is start time + timeout.

  if (state.get_exec_count() == 0)
  {
    REQUIRE_FALSE(view_1hz.empty());
    CHECK(view_1hz.front().get_exec_time().time_since_epoch() == std::chrono::seconds{1});

    REQUIRE_FALSE(view_10hz.empty());
    CHECK(view_10hz.front().get_exec_time().time_since_epoch() == std::chrono::milliseconds{100});

    REQUIRE_FALSE(view_100hz.empty());
    CHECK(view_100hz.front().get_exec_time().time_since_epoch() == std::chrono::milliseconds{10});

    REQUIRE_FALSE(view_1000hz.empty());
    CHECK(view_1000hz.front().get_exec_time().time_since_epoch() == std::chrono::milliseconds{1});
  }

  state.set_exec_count(state.get_exec_count() + 1);

  // Check that the message rates are reasonable. There is no requirement of exec order of the periodics
  // and tester cog are executed they could be off by one.

  auto count_1hz = view_1hz.size();
  auto count_10hz = view_10hz.size();
  auto count_100hz = view_100hz.size();
  auto count_1000hz = view_1000hz.size();

  CHECK(1 == count_1hz);
  CHECK(((9 <= count_10hz) && (count_10hz <= 11)));
  CHECK(((99 <= count_100hz) && (count_100hz <= 101)));
  CHECK(((999 <= count_1000hz) && (count_1000hz <= 1001)));
}

} // namespace clockwork::testing
