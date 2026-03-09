// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/dial/msg_input.hh"
#include "clockwork/examples/demo_system/imu/imu_clk_cc_dial.hh"
#include "clockwork/examples/demo_system/imu/imu_message_clk_cc.hh"
#include "clockwork/pinion/publishable.hh"

#include <chrono>
#include <cmath>
#include <ranges>

namespace clockwork::demo_system::imu
{

namespace
{

constexpr auto acceleration_mss = 0.5;

} // namespace

void execute_cog(ImuDeviceCogDial& dial)
{
  // Generate some fake accelerations
  auto& raw_msg = dial.get_outputs().get_raw_imu().message();
  const auto now = dial.get_start_time();
  raw_msg.set_time_of_validity(now);
  const auto double_time_s = std::chrono::duration_cast<std::chrono::duration<double>>(now.time_since_epoch()).count();
  raw_msg.set_x_acceleration_mss(acceleration_mss * std::sin(double_time_s) * std::sin(double_time_s / 2.0));
  raw_msg.set_y_acceleration_mss(acceleration_mss * std::sin(double_time_s) * std::cos(double_time_s / 2.0));
  raw_msg.set_z_acceleration_mss(acceleration_mss * std::cos(double_time_s));
  dial.get_outputs().get_raw_imu().mark_for_publish();
}

void execute_cog(ImuDriverCogDial& dial)
{
  const auto& raw_msg = dial.get_inputs().get_raw_imu().get_new_msgs_view().back();
  // IMU packet processing would go here.
  dial.get_outputs().get_imu().message() = raw_msg;
  dial.get_outputs().get_imu().mark_for_publish();
}

} // namespace clockwork::demo_system::imu
