// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/cog/include_common.hh"
#include "clockwork/dial/msg_input.hh"
#include "clockwork/dsl/composition/tests/support/aligned_consumer_aligner_clk_cc.hh"
#include "clockwork/dsl/composition/tests/support/aligned_consumer_cog_clk_cc_dial.hh"
#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/publishable.hh"
#include "clockwork/pinion/slot.hh"

#include <cstdint>

namespace clockwork::testing::aligned_consumer
{

void execute_cog(ConsumerCogDial& dial)
{
  auto& aligned = dial.get_inputs().get_aligned();
  const auto& sensor = aligned.get_sensor().get_latest_msg();
  const auto& camera = aligned.get_camera().get_latest_msg();

  auto echo = dial.get_outputs().get_echo();
  echo.message().set_sensor_obs_time(sensor.get_observation_time());
  echo.message().set_camera_obs_time(camera.get_observation_time());
  echo.message().set_lidar_count(static_cast<uint32_t>(aligned.get_lidar().get_view().size()));
  echo.message().set_has_radar(aligned.get_radar().get_view().size() > 0);
  echo.mark_for_publish();
}

} // namespace clockwork::testing::aligned_consumer
