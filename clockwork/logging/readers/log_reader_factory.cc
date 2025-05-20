// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/readers/log_reader_factory.hh"

#include "clockwork/logging/offboard/log_format.hh"
#include "clockwork/logging/onboard/log_format.hh"
#include "clockwork/logging/readers/mcap_log_reader.hh"
#include "clockwork/logging/readers/offboard_log_reader.hh"
#include "clockwork/logging/readers/onboard_log_reader.hh"
#include "jewels/log_cerr/log_cerr.hh"

#include <fmt10/format.h>

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace clockwork_logging
{

std::unique_ptr<AbstractLogReader> make_reader(
  std::string_view log_uri,
  std::optional<LogInterval> maybe_log_interval,
  std::optional<RelativeInterval> maybe_relative_interval,
  DecompressOption decompress_option)
{
  if (log_uri.starts_with("file:") || log_uri.starts_with("s3:"))
  {
    return std::make_unique<OffboardLogReader>(log_uri, maybe_log_interval, maybe_relative_interval, decompress_option);
  }

  auto uri_path = std::filesystem::path(log_uri);

  // If the extension is mcap then its an mcap file.
  if (uri_path.extension() == ".mcap")
  {
    return std::make_unique<McapLogReader>(log_uri, maybe_log_interval, maybe_relative_interval);
  }

  if (std::filesystem::is_directory(uri_path))
  {
    // If the uri is a directory with files named "*.log" then its an onboard log
    for (const auto& dir_entry : std::filesystem::directory_iterator{uri_path})
    {
      if (dir_entry.path().extension().string() == onboard::log_file_suffix)
      {
        return std::make_unique<OnboardLogReader>(
          log_uri, maybe_log_interval, maybe_relative_interval, decompress_option);
      }
      if (
        (dir_entry.path().filename().string() == offboard::log_metadata_filename) ||
        (dir_entry.path().filename().string() == offboard::log_union_filename))
      {
        return std::make_unique<OffboardLogReader>(
          log_uri, maybe_log_interval, maybe_relative_interval, decompress_option);
      }
    }
  }

  const auto err = fmt::format(
    "Unknown/Unsupported log '{}'.\nKnown types:\n  mcap: File with '.mcap' extension.\n"
    "  onboard: Directory containg .olog files.\n  offboard: Directory containing "
    "stack_log_metadata.pbtxt or stack_log_union.pbtxt files.\n",
    log_uri);
  jewels::log_cerr_error("{}", err);
  throw std::invalid_argument(err);
}

} // namespace clockwork_logging
