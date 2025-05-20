// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/memory/memory_resource.hh"

#include <cstdint>
#include <random>

namespace clockwork::demo_system::camera
{

/// Camera state to generate white noise for the camera images.
/// This shows how to declare cog state in C++.
class CameraState
{
public:
  /// Constructor initializes the random number generator
  /// @param[in] memory_resource Memory resource
  explicit CameraState(jewels::memory::MemoryResource memory_resource);

  ~CameraState() = default;

  CameraState(const CameraState& other) = delete;
  CameraState& operator=(const CameraState& other) = delete;
  CameraState(CameraState&&) noexcept = delete;
  CameraState& operator=(CameraState&&) noexcept = delete;

  /// Generate a random uint16_t with a gaussian distrubution
  /// @return Random number
  [[nodiscard]] uint16_t generate_noise();

private:
  /// Random device
  std::random_device rand_dev_;

  /// Random number generator
  std::mt19937 generator_;

  /// Normal distribution for white noise
  std::normal_distribution<double> dist_;
};

} // namespace clockwork::demo_system::camera
