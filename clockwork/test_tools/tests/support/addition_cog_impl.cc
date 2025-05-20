// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/dial/cond_messages_present.hh"
#include "clockwork/dial/msg_input.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "clockwork/test_tools/tests/support/addition_cog_dial.hh"
#include "clockwork/test_tools/tests/support/addition_message.hh"
#include "clockwork/test_tools/tests/support/sum_message.hh"
#include "jewels/container/circular_buffer.hh"

#include <boost/iterator/iterator_facade.hpp>

#include <ranges>

namespace clockwork::system_runner
{
void execute_cog(AdditionCogDial& dial)
{
  if (!dial.get_conditions().get_new_addition_message().is_active())
  {
    return;
  }
  for (const auto& add_msg : dial.get_inputs().get_addition_message().get_new_msgs_view())
  {
    auto sum = add_msg.get_addend_one() + add_msg.get_addend_two();
    dial.get_outputs().get_sum_message().message().set_sum(sum);
    dial.get_outputs().get_sum_message().mark_for_publish();
  }
}
} // namespace clockwork::system_runner
