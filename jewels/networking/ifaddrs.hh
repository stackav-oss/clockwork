// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/filesystem/error_code.hh"
#include "jewels/std/expected.hh"

#include <string>
#include <string_view>

namespace jewels::networking
{

/// Lookup the name of the interface with the given address.
/// @param addr_str The IPv4 address to query for.
/// @return The interface name or an error code.
jewels::expected<std::string, filesystem::ErrorCode> lookup_interface_name(std::string_view addr_str);

} // namespace jewels::networking
