// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/dial/msg_input.hh"
#include "clockwork/examples/approx_aligner/approx_aligner_msgs_clk_cc.hh"
#include "clockwork/examples/approx_aligner/tests/support/test_cogs_clk_cc_dial.hh"
#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/publishable.hh"
#include "clockwork/pinion/slot.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <ranges>

namespace clockwork
{

void execute_cog(IntMsgProducerDial& dial)
{
  const auto& start_time = dial.get_start_time();
  const auto& start_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(start_time.time_since_epoch());

  auto& out = dial.get_outputs().get_out_int();
  out.message().set_time_of_validity(start_time);
  out.message().set_int_value(static_cast<int32_t>(start_time_ms.count()));
  out.mark_for_publish();
}

void execute_cog(FloatMsgProducerDial& dial)
{
  const auto& start_time = dial.get_start_time();
  const auto& start_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(start_time.time_since_epoch());

  auto& out = dial.get_outputs().get_out_float();
  out.message().set_time_of_validity(start_time);
  out.message().set_float_value(static_cast<float>(start_time_ms.count()));
  out.mark_for_publish();
}

void execute_cog(AlignedMsgTesterDial& dial)
{
  const auto& aligned = dial.get_inputs().get_in_aligned();
  for (const auto& msg : aligned.get_new_msgs_view())
  {
    auto tov_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(msg.get_time_of_validity().time_since_epoch()).count();
    auto int_ms = msg.get_int_value();
    auto float_ms = msg.get_float_value();

    CHECK(static_cast<int32_t>(tov_ms) == int_ms);
    CHECK(static_cast<float>(tov_ms) == float_ms);
  }
}

} // namespace clockwork
