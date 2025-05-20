// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/examples/demo_system/camera/camera_state.hh"

#include <algorithm>
#include <limits>

namespace clockwork::demo_system::camera
{

namespace
{

/// White noise mean value
constexpr auto mean = 128.0;

/// White noise standard deviation
constexpr auto std_dev = mean / 2.0;

} // namespace

CameraState::CameraState(jewels::memory::MemoryResource /*memory_resource*/)
  : generator_(rand_dev_()), dist_(mean, std_dev)
{
}

[[nodiscard]] uint16_t CameraState::generate_noise()
{
  constexpr auto min_value = static_cast<double>(std::numeric_limits<uint16_t>::min());
  constexpr auto max_value = static_cast<double>(std::numeric_limits<uint16_t>::max());
  return static_cast<uint16_t>(std::max(min_value, std::min(max_value, dist_(generator_))));
}

} // namespace clockwork::demo_system::camera
