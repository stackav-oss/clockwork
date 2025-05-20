// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/dial/msg_input.hh"
#include "clockwork/examples/demo_system/localization/pose_message.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/time/sync_time.hh"

#include <cstddef>
#include <functional>
#include <map>
#include <memory_resource>

namespace clockwork::demo_system::localization
{

/// Pose filter that interpolates the vehicle position at a given time
/// from the contents of a pose message dial input.
class PoseFilter
{
public:
  /// Construct a pose filter from a PoseMessage dial input
  /// @tparam max_size Maximum size of the pose message input view
  /// @param[in] pose_input Pose message input
  template <size_t max_size>
  PoseFilter(
    jewels::memory::MemoryResource memory_resource, const MessageInputDial<Tappy<PoseMessage>, max_size>& pose_input);

  ~PoseFilter() noexcept = default;

  PoseFilter(const PoseFilter& other) = delete;
  PoseFilter& operator=(const PoseFilter& other) = delete;
  PoseFilter(PoseFilter&&) noexcept = default;
  PoseFilter& operator=(PoseFilter&&) noexcept = default;

  /// Extrapolate the location at a given time from the pose message history
  /// @param[in] timestamp Timestamp of the desired pose
  /// @return Extrapolated pose for the timestamp
  [[nodiscard]] Tappy<R3> extrapolate_location(jewels::time::SyncTime time) const;

private:
  /// Map from time of validity to pose message
  std::pmr::map<jewels::time::SyncTime, const Tappy<PoseMessage>*> pose_map_;
};

} // namespace clockwork::demo_system::localization

#include "clockwork/examples/demo_system/localization/pose_filter.inl"
