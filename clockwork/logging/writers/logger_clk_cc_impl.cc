// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/writers/logger.hh"
#include "clockwork/logging/writers/logger_clk_cc_dial.hh"
#include "clockwork/logging/writers/logger_config_clk_cc.hh"
#include "clockwork/pinion/publishable.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_unique_ptr.hh"

namespace clockwork_logging
{

void execute_cog(LoggerInitCogDial& dial)
{
  dial.get_states().get_logger_state().logger_ptr = jewels::memory::make_pmr_unique<Logger>(
    dial.get_states().get_logger_state().memory_resource,
    dial.get_states().get_logger_state().memory_resource,
    dial.get_configs().get_log_writer_config(),
    dial.get_configs().get_logger_config(),
    dial.get_configs().get_channel_rates_config());
  dial.get_outputs().get_logger_config_out().message() = dial.get_configs().get_logger_config();
  dial.get_outputs().get_logger_config_out().mark_for_publish();
}

void execute_cog(LoggerStatusPublisherCogDial& dial)
{
  const auto& logger_ptr = dial.get_states().get_logger_state().logger_ptr;
  logger_ptr->get_logger_status_message(dial.get_outputs().get_logger_status().message());
  dial.get_outputs().get_logger_status().mark_for_publish();
  if (logger_ptr->get_channel_rates_message(dial.get_outputs().get_channel_rates().message()))
  {
    dial.get_outputs().get_channel_rates().mark_for_publish();
  }
}

} // namespace clockwork_logging
