// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/examples/demo_system/localization/pose_filter.hh"

#include <chrono>
#include <compare>
#include <iterator>
#include <utility>

namespace clockwork::demo_system::localization
{

[[nodiscard]] Tappy<R3> PoseFilter::extrapolate_location(jewels::time::SyncTime time) const
{
  auto iter = pose_map_.upper_bound(time);
  if (iter == pose_map_.end())
  {
    iter = std::prev(iter);
  }
  else if (iter != pose_map_.begin())
  {
    if ((time - std::prev(iter)->first) < (iter->first - time))
    {
      iter = std::prev(iter);
    }
  }
  const auto delta_s = std::chrono::duration_cast<std::chrono::duration<double>>(time - iter->first).count();
  const auto& start_location_m = iter->second->get_location_m();
  const auto& velocity_m_per_s = iter->second->get_velocity_m_per_s();
  Tappy<R3> location_m{};
  location_m.set_x(start_location_m.get_x() + (delta_s * velocity_m_per_s.get_x()));
  location_m.set_y(start_location_m.get_y() + (delta_s * velocity_m_per_s.get_y()));
  location_m.set_z(start_location_m.get_z() + (delta_s * velocity_m_per_s.get_z()));
  return location_m;
}

} // namespace clockwork::demo_system::localization
