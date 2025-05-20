// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/simplelaunch/v1/service.pb.h" // IWYU pragma: export

#include <wise_enum.h>

#include <cstdint>

namespace jewels::simplelaunch
{
using ::jewels::simplelaunch::v1::GetProcessListResponse;
using ::jewels::simplelaunch::v1::PreLaunchInfo;
using ::jewels::simplelaunch::v1::ProcessInfo;
using ::jewels::simplelaunch::v1::SimpleLaunchCommand;

/// States that a process can be in.
WISE_ENUM_CLASS((ProcessState, uint8_t), unspecified, not_running, running, exited, crashed)
} // namespace jewels::simplelaunch
