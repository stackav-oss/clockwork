// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/std/expected.hh"

#include <cstdint>

namespace jewels::networking
{
/// Get the port assigned to a current socket.
/// TODO(OI-4001): Create an Outcome for errno.
/// @param file_descriptor The fd for the socket.
/// @return The port or an error.
/// @{
jewels::expected<uint16_t, jewels::filesystem::ErrorCode> get_assigned_port(int file_descriptor);

jewels::expected<uint16_t, jewels::filesystem::ErrorCode>
get_assigned_port(const jewels::filesystem::FileDescriptor& file_descriptor);
/// @}
} // namespace jewels::networking
