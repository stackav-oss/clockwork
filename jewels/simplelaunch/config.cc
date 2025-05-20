// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/simplelaunch/config.hh"

#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/log_cerr/log_cerr.hh"

#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/text_format.h>

#include <cerrno>
#include <string_view>

namespace jewels::simplelaunch
{
jewels::expected<Config, jewels::filesystem::ErrorCode>
load_config(jewels::filesystem::Filesystem& filesystem, const std::string_view path)
{
  Config config;

  const auto expected_config_fd = filesystem.open(path);
  if (!static_cast<bool>(expected_config_fd))
  {
    return jewels::unexpected(expected_config_fd.error());
  }

  google::protobuf::io::FileInputStream config_file_input_stream{*(expected_config_fd.value())};
  if (!google::protobuf::TextFormat::Parse(&config_file_input_stream, &config))
  {
    // If parsing fails protobuf will print an error describing why parsing failed to stderr
    jewels::log_cerr_error("Failed parse config file {}", path);
    return jewels::unexpected(jewels::filesystem::make_error_code(ENOENT));
  }

  return config;
}
} // namespace jewels::simplelaunch
