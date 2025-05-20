// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <span>

namespace jewels::simplelaunch
{

/// Config scheduling for the current process to run on specific CPUs
/// @param[in] cpus CPUs used by the process, if empty then nothing is changed
/// @return True on success, false on error
[[nodiscard]] bool set_cpu_affinity(std::span<const int32_t> cpus);

} // namespace jewels::simplelaunch
