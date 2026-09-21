// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/dial/msg_input.hh"
#include "clockwork/examples/demo_system/lidar/lidar_clk_cc_dial.hh"
#include "clockwork/examples/demo_system/lidar/lidar_message_clk_cc.hh"
#include "clockwork/examples/demo_system/lidar/lidar_state_clk_cc.hh"
#include "clockwork/examples/demo_system/localization/pose_filter.hh"
#include "clockwork/examples/demo_system/localization/pose_message_clk_cc.hh"
#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/publishable.hh"
#include "clockwork/pinion/slot.hh"
#include "jewels/container/tap/var_array.hh"
#include "jewels/math/constants.hh"
#include "jewels/memory/aligned_storage.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/uuid/uuid.hh"

#include <cmath>
#include <cstddef>
#include <span>

namespace clockwork::demo_system::lidar
{

namespace
{

/// Azimuth delta each vertical line in the lidar message
constexpr auto azimuth_delta_radians = jewels::math::constants::tau<double> /
                                       static_cast<double>(raw_messages_per_sweep) /
                                       static_cast<double>(returns_per_message);

/// Elevation delta between points in the lidar message
constexpr auto elevation_delta_radians =
  (max_elevation_radians - min_elevation_radians) / static_cast<double>(points_per_return - 1U);

/// Distance from the lidar to the ground in meters
constexpr auto lidar_height_m = 3.0;

/// Distance from the lidar to the cylinder surrounding the vehicle
constexpr auto wall_distance_m = 20.0;

/// Elevation angle where we stop seeing the ground and start seeing the wall of the cylinder (atan(-3/20))
constexpr auto max_ground_radians = -0.148890;

} // namespace

// Generate raw messagesthat show the vehicle surrounded by a cylinder
void execute_cog(LidarDeviceCogDial& dial)
{
  auto& state = dial.get_states().get_state();
  auto lidar_returns = dial.get_outputs().get_raw_lidar().message().get_mutable_returns();
  auto azimuth_radians = jewels::math::constants::tau<double> * static_cast<double>(state.get_current_sweep_index()) /
                         static_cast<double>(raw_messages_per_sweep);
  for (size_t i = 0U; i < returns_per_message; ++i)
  {
    auto& lidar_return = lidar_returns[i];
    lidar_return.set_time_of_validity(dial.get_start_time());
    lidar_return.set_azimuth_radians(static_cast<float>(azimuth_radians));
    auto elevation_radians = min_elevation_radians;
    auto& points = lidar_return.get_underlying_points();
    for (size_t j = 0U; j < points_per_return; ++j)
    {
      auto& point = points.emplace_back();
      point.set_elevation_radians(static_cast<float>(elevation_radians));
      if (elevation_radians <= max_ground_radians)
      {
        point.set_range_m(static_cast<float>(std::abs(lidar_height_m / std::sin(elevation_radians))));
      }
      else
      {
        point.set_range_m(static_cast<float>(wall_distance_m / std::cos(elevation_radians)));
      }
      elevation_radians += elevation_delta_radians;
    }
    azimuth_radians += azimuth_delta_radians;
  }
  state.set_current_sweep_index((state.get_current_sweep_index() + 1U) % raw_messages_per_sweep);
  dial.get_outputs().get_raw_lidar().mark_for_publish();
}

void execute_cog(LidarDriverCogDial& dial)
{
  const localization::PoseFilter pose_filter{dial.get_resources().get_memory(), dial.get_inputs().get_pose()};
  auto& lidar_sweep = dial.get_outputs().get_lidar_sweep().message();
  lidar_sweep.set_time_of_validity(dial.get_start_time());
  for (const auto& raw_lidar : dial.get_inputs().get_raw_lidar().get_view())
  {
    for (const auto& lidar_return : raw_lidar.get_returns())
    {
      const auto location_m = pose_filter.extrapolate_location(lidar_return.get_time_of_validity());
      for (const auto& raw_point : lidar_return.get_points())
      {
        auto& point = lidar_sweep.get_underlying_points().emplace_back();
        point.set_x(
          location_m.get_x() + (raw_point.get_range_m() * std::cos(lidar_return.get_azimuth_radians()) *
                                std::cos(raw_point.get_elevation_radians())));
        point.set_y(
          location_m.get_y() + (raw_point.get_range_m() * std::cos(lidar_return.get_azimuth_radians()) *
                                std::sin(raw_point.get_elevation_radians())));
        point.set_z(location_m.get_z() + (raw_point.get_range_m() * std::sin(raw_point.get_elevation_radians())));
      }
    }
  }
  dial.get_outputs().get_lidar_sweep().mark_for_publish();
}

} // namespace clockwork::demo_system::lidar
