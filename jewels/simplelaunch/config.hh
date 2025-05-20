// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/filesystem.hh"
#include "jewels/simplelaunch/v1/config.pb.h" // IWYU pragma: export
#include "jewels/std/expected.hh"

#include <string_view>

namespace jewels::simplelaunch
{
using ::jewels::simplelaunch::v1::AppConfig;
using ::jewels::simplelaunch::v1::Config;

/// Load configuration from a file.
/// @param filesystem The filesystem to read the config file from.
/// @param path The path to the config file.
/// @return The deserialized config or an error.
expected<Config, jewels::filesystem::ErrorCode> load_config(filesystem::Filesystem& filesystem, std::string_view path);
} // namespace jewels::simplelaunch
