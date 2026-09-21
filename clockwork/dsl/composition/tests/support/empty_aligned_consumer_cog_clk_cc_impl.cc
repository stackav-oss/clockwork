// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/dial/include_common.hh"
#include "clockwork/dsl/composition/tests/support/aligned_consumer_aligner_clk_cc.hh"
#include "clockwork/dsl/composition/tests/support/empty_aligned_consumer_cog_clk_cc_dial.hh"
#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/slot.hh"
#include "jewels/container/compare.hh"

#include <cstdint>

namespace clockwork::testing::empty_aligned_consumer
{

void execute_cog(EmptyAlignedConsumerCogDial& dial)
{
  const auto& aligned = dial.get_inputs().get_aligned();
  const auto& raw_data = dial.get_inputs().get_raw_data().get_latest_msg();

  auto echo = dial.get_outputs().get_echo();
  echo.message().set_sensor_obs_time(raw_data.get_observation_time());
  echo.message().set_lidar_count(static_cast<uint32_t>(aligned.get_lidar().get_view().size()));
  echo.message().set_has_radar(!aligned.get_radar().get_view().empty());
  echo.mark_for_publish();
}

} // namespace clockwork::testing::empty_aligned_consumer
