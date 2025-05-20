// IWYU pragma: private, include "clockwork/examples/demo_system/localization/pose_filter.hh"
#pragma once

#include "clockwork/examples/demo_system/localization/pose_filter.hh"

#include "clockwork/dial/msg_input.hh"
#include "clockwork/examples/demo_system/localization/pose_message.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/memory/memory_resource.hh"

#include <fmt10/format.h>

#include <cstddef>
#include <stdexcept>

namespace clockwork::demo_system::localization
{

template <size_t max_size>
PoseFilter::PoseFilter(
  jewels::memory::MemoryResource memory_resource, const MessageInputDial<Tappy<PoseMessage>, max_size>& pose_input)
  : pose_map_(memory_resource)
{
  if (pose_input.get_view().size() < 2U)
  {
    throw std::runtime_error(
      fmt::format("Pose filter requires at least two points to extrapolate, have {}", pose_input.get_view().size()));
  }
  for (const auto& pose_msg : pose_input.get_view())
  {
    pose_map_.emplace(pose_msg.get_time_of_validity(), &pose_msg);
  }
}

} // namespace clockwork::demo_system::localization
