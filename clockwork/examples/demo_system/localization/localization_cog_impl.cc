// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/dial/msg_input.hh"
#include "clockwork/examples/demo_system/gps/gps_message.hh"
#include "clockwork/examples/demo_system/imu/imu_message.hh"
#include "clockwork/examples/demo_system/localization/localization_cog_dial.hh"
#include "clockwork/examples/demo_system/localization/localization_state.hh"
#include "clockwork/examples/demo_system/localization/pose_message.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "jewels/container/circular_buffer.hh"
#include "jewels/math/constants.hh"

#include <boost/iterator/iterator_facade.hpp>

#include <chrono>
#include <cmath>
#include <compare>
#include <iterator>

namespace clockwork::demo_system::localization
{

namespace
{

/// Radians per degree of arc
constexpr auto radians_per_degree = jewels::math::constants::pi<double> / 180.0;

/// Diameter of the earth in meters because this demo approximates earth as a sphere
constexpr auto earth_radius_m = 6378137.0;

} // namespace

void execute_cog(LocalizationInitCogDial& dial)
{
  // We could have used the default initializer and not had an init cog at all.
  // This is here to show how to use an init cog to initialize state.
  dial.get_states().get_state().reset_maybe_time_of_validity();
}

// This code is overly simplified for demo purposes
void execute_cog(LocalizationImuCogDial& dial)
{
  auto& state = dial.get_states().get_state();
  // We need at least two IMU messages and one GPS message before we can start
  if (dial.get_inputs().get_imu().get_view().size() >= 2U && state.has_maybe_time_of_validity())
  {
    const auto& curr_imu = *std::prev(dial.get_inputs().get_imu().end());
    const auto& prev_imu = *std::prev(std::prev(dial.get_inputs().get_imu().end()));
    const auto imu_delta_t = std::chrono::duration_cast<std::chrono::duration<double>>(
      curr_imu.get_time_of_validity() - prev_imu.get_time_of_validity());
    auto& velocity_m_per_s = state.get_mutable_velocity_m_per_s();
    velocity_m_per_s.set_x(velocity_m_per_s.get_x() + (imu_delta_t.count() * curr_imu.get_x_acceleration_mss()));
    velocity_m_per_s.set_y(velocity_m_per_s.get_y() + (imu_delta_t.count() * curr_imu.get_y_acceleration_mss()));
    velocity_m_per_s.set_z(velocity_m_per_s.get_z() + (imu_delta_t.count() * curr_imu.get_z_acceleration_mss()));
    const auto time_of_validity = state.value_maybe_time_of_validity();
    if (curr_imu.get_time_of_validity() >= time_of_validity)
    {
      const auto delta_t =
        std::chrono::duration_cast<std::chrono::duration<double>>(curr_imu.get_time_of_validity() - time_of_validity);
      state.get_mutable_location_m().set_x(
        state.get_location_m().get_x() + (delta_t.count() * velocity_m_per_s.get_x()));
      state.get_mutable_location_m().set_y(
        state.get_location_m().get_y() + (delta_t.count() * velocity_m_per_s.get_y()));
      state.get_mutable_location_m().set_z(
        state.get_location_m().get_z() + (delta_t.count() * velocity_m_per_s.get_z()));
      state.set_maybe_time_of_validity(curr_imu.get_time_of_validity());
    }
    auto& pose_msg = dial.get_outputs().get_pose().message();
    pose_msg.set_time_of_validity(state.value_maybe_time_of_validity());
    pose_msg.set_location_m(state.get_location_m());
    pose_msg.set_velocity_m_per_s(state.get_velocity_m_per_s());
    dial.get_outputs().get_pose().mark_for_publish();
  }
}

// This code is overly simplified for demo purposes
void execute_cog(LocalizationGpsCogDial& dial)
{
  // We need two GPS messages before we can start
  if (dial.get_inputs().get_gps().get_view().size() < 2U)
  {
    return;
  }
  auto& state = dial.get_states().get_state();
  const auto& curr_gps = *std::prev(dial.get_inputs().get_gps().end());
  const auto& prev_gps = *std::prev(std::prev(dial.get_inputs().get_gps().end()));
  const auto curr_x_location_m = earth_radius_m * std::cos(curr_gps.get_latitude_deg() * radians_per_degree) *
                                 std::cos(curr_gps.get_longitude_deg() * radians_per_degree);
  const auto curr_y_location_m = earth_radius_m * std::cos(curr_gps.get_latitude_deg() * radians_per_degree) *
                                 std::sin(curr_gps.get_longitude_deg() * radians_per_degree);
  const auto curr_z_location_m = earth_radius_m * std::sin(curr_gps.get_latitude_deg() * radians_per_degree);
  const auto prev_x_location_m = earth_radius_m * std::cos(prev_gps.get_latitude_deg() * radians_per_degree) *
                                 std::cos(prev_gps.get_longitude_deg() * radians_per_degree);
  const auto prev_y_location_m = earth_radius_m * std::cos(prev_gps.get_latitude_deg() * radians_per_degree) *
                                 std::sin(prev_gps.get_longitude_deg() * radians_per_degree);
  const auto prev_z_location_m = earth_radius_m * std::sin(prev_gps.get_latitude_deg() * radians_per_degree);
  const auto gps_delta_t = std::chrono::duration_cast<std::chrono::duration<double>>(
    curr_gps.get_time_of_validity() - prev_gps.get_time_of_validity());
  const auto x_velocity_m_per_s = (curr_x_location_m - prev_x_location_m) / gps_delta_t.count();
  const auto y_velocity_m_per_s = (curr_y_location_m - prev_y_location_m) / gps_delta_t.count();
  const auto z_velocity_m_per_s = (curr_z_location_m - prev_z_location_m) / gps_delta_t.count();
  if (!state.has_maybe_time_of_validity())
  {
    state.set_maybe_time_of_validity(curr_gps.get_time_of_validity());
    state.get_mutable_location_m().set_x(curr_x_location_m);
    state.get_mutable_location_m().set_y(curr_y_location_m);
    state.get_mutable_location_m().set_z(curr_z_location_m);
    state.get_mutable_velocity_m_per_s().set_x(x_velocity_m_per_s);
    state.get_mutable_velocity_m_per_s().set_y(y_velocity_m_per_s);
    state.get_mutable_velocity_m_per_s().set_z(z_velocity_m_per_s);
  }
  state.get_mutable_location_m().set_x((state.get_location_m().get_x() + curr_x_location_m) / 2.0);
  state.get_mutable_location_m().set_y((state.get_location_m().get_y() + curr_y_location_m) / 2.0);
  state.get_mutable_location_m().set_z((state.get_location_m().get_z() + curr_z_location_m) / 2.0);
  state.get_mutable_velocity_m_per_s().set_x((state.get_velocity_m_per_s().get_x() + x_velocity_m_per_s) / 2.0);
  state.get_mutable_velocity_m_per_s().set_y((state.get_velocity_m_per_s().get_y() + y_velocity_m_per_s) / 2.0);
  state.get_mutable_velocity_m_per_s().set_z((state.get_velocity_m_per_s().get_z() + z_velocity_m_per_s) / 2.0);
  if (curr_gps.get_time_of_validity() < state.value_maybe_time_of_validity())
  {
    const auto delta_t = std::chrono::duration_cast<std::chrono::duration<double>>(
      state.value_maybe_time_of_validity() - curr_gps.get_time_of_validity());
    state.get_mutable_location_m().set_x(
      state.get_location_m().get_x() + (delta_t.count() * state.get_velocity_m_per_s().get_x()));
    state.get_mutable_location_m().set_y(
      state.get_location_m().get_y() + (delta_t.count() * state.get_velocity_m_per_s().get_y()));
    state.get_mutable_location_m().set_z(
      state.get_location_m().get_z() + (delta_t.count() * state.get_velocity_m_per_s().get_z()));
    state.set_maybe_time_of_validity(curr_gps.get_time_of_validity());
  }
}

} // namespace clockwork::demo_system::localization
