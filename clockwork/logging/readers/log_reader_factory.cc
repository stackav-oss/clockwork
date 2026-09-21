// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/readers/log_reader_factory.hh"

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/offboard/chunk_reader_writer_factory.hh"
#include "clockwork/logging/offboard/log_format.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "clockwork/logging/onboard/buffered_reader.hh"
#include "clockwork/logging/onboard/log_format.hh"
#include "clockwork/logging/onboard/offboard_buffered_reader.hh"
#include "clockwork/logging/readers/mcap_log_reader.hh"
#include "clockwork/logging/readers/offboard_log_reader.hh"
#include "clockwork/logging/readers/onboard_log_reader.hh"
#include "jewels/container/circular_buffer.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"

#include <fmt/format.h>
#include <gsl/util>

#include <filesystem>
#include <memory_resource>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace clockwork_logging
{

namespace
{

constexpr auto error_message_format =
  "Unknown/Unsupported log '{}'.\nKnown types:\n  mcap: File with '.mcap' extension.\n  onboard: Directory containg "
  ".olog files.\n  offboard: Directory containing onboard or offboard log files, stack_log_metadata.pbtxt, "
  "stack_log_amendment.pbtxt or stack_log_union.pbtxt.\n";

/// Construct a log reader from a URI
/// @param[in] log_uri Log URI
/// @param[in] maybe_log_interval The interval to read from the log
/// @param[in] maybe_relative_interval The interval to read from the log relative to the sart of the log
/// @param[in] decompress_option Option for whether to decompress lite-compressed messages found in the log
/// @return Log reader pointer
std::unique_ptr<AbstractLogReader> make_reader_from_uri(
  std::string_view log_uri,
  std::optional<LogInterval> maybe_log_interval,
  std::optional<RelativeInterval> maybe_relative_interval,
  DecompressOption decompress_option)
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  auto chunk_reader_factory =
    std::make_shared<clockwork_logging::offboard::ChunkReaderWriterFactory<>>(memory_resource);
  const auto list_result = chunk_reader_factory->list_log_files(log_uri, "");
  if (!list_result)
  {
    const auto msg = fmt::format("Cannot access '{}': {}", log_uri, list_result.error());
    jewels::log_cerr_error("{}", msg);
    throw std::invalid_argument(msg);
  }
  for (const auto& file_uri : list_result.value())
  {
    if (
      (file_uri.filename() == offboard::log_metadata_filename) ||
      (file_uri.filename() == offboard::log_amendment_filename) ||
      (file_uri.filename() == offboard::log_union_filename) || (file_uri.extension() == offboard::log_file_suffix))
    {
      return std::make_unique<OffboardLogReader>(
        log_uri, maybe_log_interval, maybe_relative_interval, decompress_option, chunk_reader_factory);
    }
    if (file_uri.extension() == onboard::log_file_suffix)
    {
      auto offboard_buffered_reader =
        std::make_shared<clockwork_logging::onboard::OffboardBufferedReader<OnboardLogReaderPolicy>>(
          memory_resource, std::move(chunk_reader_factory));
      return std::make_unique<OffboardOnboardLogReader>(
        log_uri, maybe_log_interval, maybe_relative_interval, decompress_option, std::move(offboard_buffered_reader));
    }
  }
  const auto err = fmt::format(error_message_format, log_uri);
  jewels::log_cerr_error("{}", err);
  throw std::invalid_argument(err);
}

/// Construct a log reader from a file path
/// @param[in] log_uri Log URI
/// @param[in] maybe_log_interval The interval to read from the log
/// @param[in] maybe_relative_interval The interval to read from the log relative to the sart of the log
/// @param[in] decompress_option Option for whether to decompress lite-compressed messages found in the log
/// @return Log reader pointer
std::unique_ptr<AbstractLogReader> make_reader_from_path(
  std::string_view log_uri,
  std::optional<LogInterval> maybe_log_interval,
  std::optional<RelativeInterval> maybe_relative_interval,
  DecompressOption decompress_option)
{
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
        const auto memory_resource = jewels::memory::MemoryResource{std::pmr::new_delete_resource()};
        auto buffered_reader =
          std::make_shared<clockwork_logging::onboard::BufferedReader<OnboardLogReaderPolicy>>(memory_resource);
        return std::make_unique<OnboardLogReader>(
          log_uri, maybe_log_interval, maybe_relative_interval, decompress_option, std::move(buffered_reader));
      }
      if (
        (dir_entry.path().filename().string() == offboard::log_metadata_filename) ||
        (dir_entry.path().filename().string() == offboard::log_amendment_filename) ||
        (dir_entry.path().filename().string() == offboard::log_union_filename) ||
        (dir_entry.path().extension().string() == offboard::log_file_suffix))
      {
        const auto memory_resource = jewels::memory::MemoryResource{std::pmr::new_delete_resource()};
        auto chunk_reader_factory =
          std::make_shared<clockwork_logging::offboard::ChunkReaderWriterFactory<>>(memory_resource);
        return std::make_unique<OffboardLogReader>(
          log_uri, maybe_log_interval, maybe_relative_interval, decompress_option, std::move(chunk_reader_factory));
      }
    }
  }

  const auto err = fmt::format(error_message_format, log_uri);
  jewels::log_cerr_error("{}", err);
  throw std::invalid_argument(err);
}

} // namespace

std::unique_ptr<AbstractLogReader> make_reader(
  std::string_view log_uri,
  std::optional<LogInterval> maybe_log_interval,
  std::optional<RelativeInterval> maybe_relative_interval,
  DecompressOption decompress_option)
{
  if (log_uri.starts_with("file:") || log_uri.starts_with("s3:"))
  {
    return make_reader_from_uri(log_uri, maybe_log_interval, maybe_relative_interval, decompress_option);
  }
  return make_reader_from_path(log_uri, maybe_log_interval, maybe_relative_interval, decompress_option);
}

} // namespace clockwork_logging
