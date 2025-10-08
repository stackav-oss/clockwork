// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/dial/msg_input.hh"
#include "clockwork/examples/helloworld/cogs_dial.hh"
#include "clockwork/examples/helloworld/schema.hh"
#include "clockwork/examples/helloworld/state.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "jewels/container/circular_buffer.hh"
#include "jewels/log_cerr/log_cerr.hh"

#include <chrono>
#include <ranges>

namespace clockwork::helloworld
{

void execute_cog(ProducerCogDial& dial)
{
  // We can access time:
  const auto now = dial.get_start_time();
  // And we can access our output message:
  auto& output = dial.get_outputs().get_my_output().message();
  // And set fields:
  auto& helloworld_state = dial.get_states().get_my_state();
  auto& counter = helloworld_state.get_mutable_counter();
  output.set_a_field(counter);
  // And finally, mark the message as ready to publish:
  dial.get_outputs().get_my_output().mark_for_publish();
  jewels::log_cerr_info(
    "ProducerCog sending message with a_field = {} at time {}", counter++, now.time_since_epoch().count());
}

void execute_cog(ConsumerCogDial& dial)
{
  for (const auto& msg : dial.get_inputs().get_input_a().get_new_msgs_view())
  {
    jewels::log_cerr_info("Received a new message on InputA: {}", msg.get_a_field());
  }
}
} // namespace clockwork::helloworld
