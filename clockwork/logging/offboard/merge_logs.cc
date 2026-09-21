// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/offboard/merge_logs.hh"

#include "clockwork/logging/log_uuid.hh"
#include "clockwork/logging/offboard/chunk_reader_writer_factory.hh"
#include "clockwork/logging/offboard/copy_log.hh"
#include "clockwork/logging/offboard/log_format.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "clockwork/logging/offboard/v1/log_amendment.pb.h"
#include "clockwork/logging/offboard/v1/log_metadata.pb.h"
#include "clockwork/logging/offboard/v1/log_union.pb.h"
#include "clockwork/logging/readers/types.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"

#include <google/protobuf/repeated_ptr_field.h>

#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace clockwork_logging::offboard
{

using jewels::InOut;
using jewels::ok;
using jewels::Out;

namespace
{

/// Log union file header
constexpr auto file_header = R"(# proto-file: clockwork/logging/offboard/v1/log_union.proto
# proto-message: LogUnion
)";

/// RAII temporary log directory
class TemporaryLogDirectory
{
public:
  /// Constructor creates a temporary log directory
  TemporaryLogDirectory();

  /// Destructor deletes the temporary log directory
  ~TemporaryLogDirectory();

  TemporaryLogDirectory(const TemporaryLogDirectory&) = delete;
  TemporaryLogDirectory& operator=(const TemporaryLogDirectory&) = delete;
  TemporaryLogDirectory(TemporaryLogDirectory&&) = delete;
  TemporaryLogDirectory& operator=(TemporaryLogDirectory&&) = delete;

  /// Get the temporary log directory path
  /// @return Log directory path
  [[nodiscard]] const std::filesystem::path& get_path() const;

private:
  /// Temporary directory path
  std::filesystem::path path_;
};

TemporaryLogDirectory::TemporaryLogDirectory()
{
  path_ = std::filesystem::temp_directory_path() / LogUuid::random_uuid().to_string();
}

TemporaryLogDirectory::~TemporaryLogDirectory()
{
  std::filesystem::remove_all(path_);
}

[[nodiscard]] const std::filesystem::path& TemporaryLogDirectory::get_path() const
{
  return path_;
}

/// Test whether a log exists at the specified URI
/// @param[in] source_uri Source log URI
/// @param[in] chunk_reader_writer_factory Chunk reader writer factory
/// @return Success if a log exists, LogError on faulure
LogOutcome source_log_exists(const LogUri& source_uri, ChunkReaderWriterFactory<>& chunk_reader_writer_factory)
{
  const auto metadata_uri = source_uri / log_metadata_filename;
  const auto exists_result = chunk_reader_writer_factory.exists(metadata_uri.string());
  if (!exists_result)
  {
    return LogError::not_a_log;
  }
  if (!exists_result.value())
  {
    if (const auto list_result = chunk_reader_writer_factory.list_log_files(source_uri.string());
        !list_result || list_result->empty())
    {
      return list_result ? LogError::not_a_log : list_result.error();
    }
  }
  return LogError::success;
}

/// Process a source log URI for a log union
/// @param[in] memory_resource Memory resource
/// @param[in] source_uri Source log URI
/// @param[in] excluded_channels Channels to exclude from the source log
/// @param[in] chunk_reader_writer_factory Chunk reader writer factory
/// @param[in,out] processed_uri_map Map from URI to log union entry for processed URIs
/// @param[in,out] pending_uri_map Map from URI to excluded channels for pending URIs
/// @return Success or LogError on failure
LogOutcome process_source_log_union_uri(
  jewels::memory::MemoryResource memory_resource,
  const LogUri& source_uri,
  const std::set<std::string>& excluded_channels,
  ChunkReaderWriterFactory<>& chunk_reader_writer_factory,
  InOut<std::map<LogUri, std::set<std::string>>> processed_uri_map,
  InOut<std::map<LogUri, std::set<std::string>>> pending_uri_map)
{
  const auto union_uri = source_uri / log_union_filename;
  const auto read_result =
    chunk_reader_writer_factory.read_text_proto<::clockwork::logging::offboard::v1::LogUnion>(union_uri.string());
  if (!read_result)
  {
    jewels::log_cerr_error("Failed to read '{}': {}", union_uri.string(), read_result.error());
    return read_result.error();
  }
  const auto& log_union_metadata = read_result.value();
  for (const auto& union_entry : log_union_metadata.log_union_entry())
  {
    LogUri entry_uri{memory_resource};
    if (union_entry.has_absolute_path())
    {
      auto make_result = LogUri::try_make(union_entry.absolute_path(), memory_resource);
      if (!make_result)
      {
        jewels::log_cerr_error("Invalid uri: {}", union_entry.absolute_path());
        return LogError::invalid_log_uri;
      }
      entry_uri = std::move(make_result).value();
      if (!entry_uri.has_filename())
      {
        entry_uri = entry_uri.parent_uri();
      }
    }
    else
    {
      entry_uri = source_uri.apply_relative_path(union_entry.relative_path());
    }
    if (const auto log_exists_outcome = source_log_exists(entry_uri, chunk_reader_writer_factory);
        !ok(log_exists_outcome))
    {
      return log_exists_outcome;
    }
    if (!processed_uri_map->contains(entry_uri))
    {
      auto entry_excluded_channels = excluded_channels;
      for (const auto& excluded_channel : union_entry.excluded_channel())
      {
        entry_excluded_channels.emplace(excluded_channel);
      }
      pending_uri_map->emplace(std::move(entry_uri), std::move(entry_excluded_channels));
    }
  }
  return LogError::success;
}

/// Process a source log URI for a log amendment
/// @param[in] memory_resource Memory resource
/// @param[in] source_uri Source log URI
/// @param[in] excluded_channels Channels to exclude from the source log
/// @param[in] chunk_reader_writer_factory Chunk reader writer factory
/// @param[in,out] processed_uri_map Map from URI to log union entry for processed URIs
/// @param[in,out] pending_uri_map Map from URI to excluded channels for pending URIs
/// @return Success or LogError on failure
LogOutcome process_source_log_amendment_uri(
  jewels::memory::MemoryResource memory_resource,
  const LogUri& source_uri,
  const std::set<std::string>& excluded_channels,
  ChunkReaderWriterFactory<>& chunk_reader_writer_factory,
  InOut<std::map<LogUri, std::set<std::string>>> processed_uri_map,
  InOut<std::map<LogUri, std::set<std::string>>> pending_uri_map)
{
  const auto amendment_uri = source_uri / log_amendment_filename;
  const auto read_result =
    chunk_reader_writer_factory.read_text_proto<::clockwork::logging::offboard::v1::LogAmendment>(
      amendment_uri.string());
  if (!read_result)
  {
    jewels::log_cerr_error("Failed to read '{}': {}", amendment_uri.string(), read_result.error());
    return read_result.error();
  }
  const auto& log_amendment_metadata = read_result.value();
  LogUri amended_uri{memory_resource};
  if (log_amendment_metadata.amended_log_path().has_absolute_path())
  {
    auto make_result = LogUri::try_make(log_amendment_metadata.amended_log_path().absolute_path(), memory_resource);
    if (!make_result)
    {
      jewels::log_cerr_error("Invalid uri: {}", log_amendment_metadata.amended_log_path().absolute_path());
      return LogError::invalid_log_uri;
    }
    amended_uri = make_result.value();
    if (!amended_uri.has_filename())
    {
      amended_uri = amended_uri.parent_uri();
    }
  }
  else
  {
    amended_uri = source_uri.apply_relative_path(log_amendment_metadata.amended_log_path().relative_path());
  }
  if (!processed_uri_map->contains(amended_uri))
  {
    auto amended_excluded_channels = excluded_channels;
    for (const auto& excluded_channel : log_amendment_metadata.amended_log_path().excluded_channel())
    {
      amended_excluded_channels.emplace(excluded_channel);
    }
    pending_uri_map->emplace(std::move(amended_uri), std::move(amended_excluded_channels));
  }
  processed_uri_map->emplace(source_uri, excluded_channels);
  return LogError::success;
}

/// Process a source log URI
/// @param[in] memory_resource Memory resource
/// @param[in] source_uri Source log URI
/// @param[in] excluded_channels Channels to exclude from the source log
/// @param[in] chunk_reader_writer_factory Chunk reader writer factory
/// @param[in,out] processed_uri_map Map from URI to log union entry for processed URIs
/// @param[in,out] pending_uri_map Map from URI to excluded channels for pending URIs
/// @return Success or LogError on failure
LogOutcome process_source_uri(
  jewels::memory::MemoryResource memory_resource,
  const LogUri& source_uri,
  const std::set<std::string>& excluded_channels,
  ChunkReaderWriterFactory<>& chunk_reader_writer_factory,
  InOut<std::map<LogUri, std::set<std::string>>> processed_uri_map,
  InOut<std::map<LogUri, std::set<std::string>>> pending_uri_map)
{
  const auto union_uri = source_uri / log_union_filename;
  const auto union_exists_result = chunk_reader_writer_factory.exists(union_uri.string());
  if (!union_exists_result)
  {
    jewels::log_cerr_error("Cannot access source URI '{}': {}", source_uri.string(), union_exists_result.error());
    return union_exists_result.error();
  }
  if (union_exists_result.value())
  {
    return process_source_log_union_uri(
      memory_resource,
      source_uri,
      excluded_channels,
      chunk_reader_writer_factory,
      InOut{*processed_uri_map},
      InOut{*pending_uri_map});
  }
  const auto amendment_uri = source_uri / log_amendment_filename;
  const auto amendment_exists_result = chunk_reader_writer_factory.exists(amendment_uri.string());
  if (!amendment_exists_result)
  {
    jewels::log_cerr_error("Cannot access source URI '{}': {}", source_uri.string(), amendment_exists_result.error());
    return amendment_exists_result.error();
  }
  if (amendment_exists_result.value())
  {
    return process_source_log_amendment_uri(
      memory_resource,
      source_uri,
      excluded_channels,
      chunk_reader_writer_factory,
      InOut{*processed_uri_map},
      InOut{*pending_uri_map});
  }
  if (const auto log_exists_outcome = source_log_exists(source_uri, chunk_reader_writer_factory);
      !ok(log_exists_outcome))
  {
    return log_exists_outcome;
  }
  processed_uri_map->emplace(source_uri, excluded_channels);
  return LogError::success;
}

/// Create a union of the logs to be merged, removing duplicates so that each message is read exactly once.
/// @param[in] memory_resource Memory resource
/// @param[in] source_uris Source log URIs
/// @param[in] chunk_reader_writer_factory Chunk reader writer factory
/// @param[out] log_union_metadata Log union metadata
/// @return Success or LogError on failure
LogOutcome make_merge_union(
  jewels::memory::MemoryResource memory_resource,
  std::span<std::string_view> source_uris,
  ChunkReaderWriterFactory<>& chunk_reader_writer_factory,
  Out<::clockwork::logging::offboard::v1::LogUnion> log_union_metadata)
{
  std::map<LogUri, std::set<std::string>> processed_uri_map;
  std::map<LogUri, std::set<std::string>> pending_uri_map;
  for (const auto& source_uri_str : source_uris)
  {
    auto make_result = LogUri::try_make(source_uri_str, memory_resource);
    if (!make_result)
    {
      jewels::log_cerr_error("Invalid uri: {}", source_uri_str);
      return LogError::invalid_log_uri;
    }
    auto& source_uri = make_result.value();
    if (!source_uri.has_filename())
    {
      source_uri = source_uri.parent_uri();
    }
    pending_uri_map.emplace(std::move(source_uri), std::set<std::string>{});
  }
  while (!pending_uri_map.empty())
  {
    auto pending_iter = pending_uri_map.begin();
    const auto source_uri = pending_iter->first;
    const auto excluded_channels = std::move(pending_iter->second);
    pending_uri_map.erase(pending_iter);
    if (const auto process_outcome = process_source_uri(
          memory_resource,
          source_uri,
          excluded_channels,
          chunk_reader_writer_factory,
          InOut{processed_uri_map},
          InOut{pending_uri_map});
        !ok(process_outcome))
    {
      return process_outcome;
    }
  }
  log_union_metadata->Clear();
  log_union_metadata->set_is_merge_union(true);
  for (const auto& [uri, excluded_channels] : processed_uri_map)
  {
    auto& log_union_entry = *log_union_metadata->mutable_log_union_entry()->Add();
    log_union_entry.set_absolute_path(uri.string());
    for (const auto& channel : excluded_channels)
    {
      log_union_entry.add_excluded_channel(channel);
    }
  }
  return LogError::success;
}

} // namespace

[[nodiscard]] LogExpected<void> write_merge_union(
  jewels::memory::MemoryResource memory_resource, std::span<std::string_view> source_uris, std::string_view output_path)
{
  if (source_uris.empty())
  {
    jewels::log_cerr_error("Source URIs cannot be empty");
    return jewels::unexpected(LogError::invalid_argument);
  }
  const auto make_uri_result = LogUri::try_make(output_path, memory_resource);
  if (!make_uri_result)
  {
    jewels::log_cerr_error("Invalid output path");
    return jewels::unexpected(LogError::invalid_log_uri);
  }
  const auto& output_uri = make_uri_result.value();
  ChunkReaderWriterFactory chunk_reader_writer_factory{memory_resource};
  if (const auto exists_result = chunk_reader_writer_factory.exists(output_path);
      exists_result && exists_result.value())
  {
    return jewels::unexpected(LogError::file_exists);
  }
  ::clockwork::logging::offboard::v1::LogUnion log_union_metadata;
  const auto make_union_outcome =
    make_merge_union(memory_resource, source_uris, chunk_reader_writer_factory, Out{log_union_metadata});
  if (!ok(make_union_outcome))
  {
    return jewels::unexpected(make_union_outcome.get());
  }
  if (const auto create_result = chunk_reader_writer_factory.create_directories(output_path); !create_result)
  {
    return jewels::unexpected(create_result.error());
  }
  return chunk_reader_writer_factory.write_text_proto(
    (output_uri / log_union_filename).string(), file_header, log_union_metadata);
}

[[nodiscard]] LogExpected<void> merge_logs(
  jewels::memory::MemoryResource memory_resource,
  std::span<std::string_view> source_uris,
  std::string_view dest_uri,
  const std::optional<std::pmr::unordered_set<std::pmr::string>>& maybe_desired_channels,
  const std::optional<RelativeInterval>& maybe_log_interval,
  std::string_view writer_config_str)
{
  const TemporaryLogDirectory temp_log_dir;
  const auto& temp_log_dir_str = temp_log_dir.get_path().string();
  const auto make_uri_result = LogUri::try_make(temp_log_dir_str, memory_resource);
  if (!make_uri_result)
  {
    jewels::log_cerr_error("Failed to make temp log_uri for {}", temp_log_dir_str);
    return jewels::unexpected(LogError::invalid_log_uri);
  }
  const auto union_result = write_merge_union(memory_resource, source_uris, temp_log_dir_str);
  if (!union_result)
  {
    return jewels::unexpected(union_result.error());
  }
  return copy_log(
    memory_resource, temp_log_dir_str, dest_uri, maybe_desired_channels, {}, maybe_log_interval, writer_config_str);
}

} // namespace clockwork_logging::offboard
