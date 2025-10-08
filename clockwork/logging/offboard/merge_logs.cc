// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/offboard/merge_logs.hh"

#include "clockwork/logging/log_uuid.hh"
#include "clockwork/logging/offboard/chunk_reader_writer_factory.hh"
#include "clockwork/logging/offboard/copy_log.hh"
#include "clockwork/logging/offboard/log_format.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "clockwork/logging/offboard/v1/log_union.pb.h"
#include "clockwork/logging/readers/types.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"

#include <google/protobuf/repeated_ptr_field.h>

#include <filesystem>
#include <list>
#include <optional>
#include <set>
#include <string>
#include <utility>

namespace clockwork_logging::offboard
{

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

/// Process a source log URI
/// @param[in] memory_resource Memory resource
/// @param[in] source_uri Source log URI
/// @param[in] chunk_reader_writer_factory Chunk reader writer factory
/// @param[in,out] uri_set Set of URIs that have been processed
/// @param[in,out] source_uri_list List of URIs to be processed
[[nodiscard]] LogExpected<void> process_source_uri(
  jewels::memory::MemoryResource memory_resource,
  const LogUri& source_uri,
  ChunkReaderWriterFactory& chunk_reader_writer_factory,
  std::pmr::set<LogUri>& uri_set,
  std::pmr::list<LogUri>& source_uri_list)
{
  const auto union_uri = source_uri / log_union_filename;
  const auto union_exists_result = chunk_reader_writer_factory.exists(union_uri.string());
  if (!union_exists_result)
  {
    jewels::log_cerr_error("Cannot access source URI '{}': {}", source_uri.string(), union_exists_result.error());
    return jewels::unexpected(union_exists_result.error());
  }
  if (!union_exists_result.value())
  {
    const auto metadata_uri = source_uri / log_metadata_filename;
    if (const auto exists_result = chunk_reader_writer_factory.exists(metadata_uri.string());
        !exists_result || !exists_result.value())
    {
      jewels::log_cerr_error("Cannot merge source URI '{}': Not a log", source_uri.string());
      return jewels::unexpected(LogError::not_a_log);
    }
    if (!uri_set.contains(source_uri))
    {
      uri_set.emplace(source_uri);
    }
  }
  else
  {
    const auto read_result =
      chunk_reader_writer_factory.read_text_proto<::clockwork::logging::offboard::v1::LogUnion>(union_uri.string());
    if (!read_result)
    {
      jewels::log_cerr_error("Failed to read '{}': {}", union_uri.string(), read_result.error());
      return jewels::unexpected(read_result.error());
    }
    for (const auto& union_entry : read_result.value().log_union_entry())
    {
      if (union_entry.has_absolute_path())
      {
        auto make_result = LogUri::try_make(union_entry.absolute_path(), memory_resource);
        if (!make_result)
        {
          jewels::log_cerr_error("Invalid uri: {}", union_entry.absolute_path());
          return jewels::unexpected(LogError::invalid_log_uri);
        }
        auto& entry_uri = make_result.value();
        if (!entry_uri.has_filename())
        {
          entry_uri = entry_uri.parent_uri();
        }
        source_uri_list.emplace_back(std::move(entry_uri));
      }
      else
      {
        source_uri_list.emplace_back(source_uri.apply_relative_path(union_entry.relative_path()));
      }
    }
  }
  return {};
}

/// Create a union of the logs to be merged, removing duplicates so that each message is read exactly once.
/// @param[in] memory_resource Memory resource
/// @param[in] source_uris Source log URIs
/// @param[in] chunk_reader_writer_factory Chunk reader writer factory
[[nodiscard]] LogExpected<::clockwork::logging::offboard::v1::LogUnion> make_merge_union(
  jewels::memory::MemoryResource memory_resource,
  std::span<std::string_view> source_uris,
  ChunkReaderWriterFactory& chunk_reader_writer_factory)
{
  std::pmr::set<LogUri> uri_set{memory_resource};
  std::pmr::list<LogUri> source_uri_list{memory_resource};
  for (const auto& source_uri_str : source_uris)
  {
    auto make_result = LogUri::try_make(source_uri_str, memory_resource);
    if (!make_result)
    {
      jewels::log_cerr_error("Invalid uri: {}", source_uri_str);
      return jewels::unexpected(LogError::invalid_log_uri);
    }
    auto& source_uri = make_result.value();
    if (!source_uri.has_filename())
    {
      source_uri = source_uri.parent_uri();
    }
    source_uri_list.emplace_back(std::move(source_uri));
  }
  while (!source_uri_list.empty())
  {
    const auto source_uri = std::move(source_uri_list.front());
    source_uri_list.pop_front();
    if (const auto process_result =
          process_source_uri(memory_resource, source_uri, chunk_reader_writer_factory, uri_set, source_uri_list);
        !process_result)
    {
      return jewels::unexpected(process_result.error());
    }
  }
  ::clockwork::logging::offboard::v1::LogUnion merge_protobuf;
  for (const auto& uri : uri_set)
  {
    merge_protobuf.mutable_log_union_entry()->Add()->set_absolute_path(std::string{uri.string()});
  }
  return merge_protobuf;
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
  const auto make_union_result = make_merge_union(memory_resource, source_uris, chunk_reader_writer_factory);
  if (!make_union_result)
  {
    return jewels::unexpected(make_union_result.error());
  }
  if (const auto create_result = chunk_reader_writer_factory.create_directories(output_path); !create_result)
  {
    return jewels::unexpected(create_result.error());
  }
  return chunk_reader_writer_factory.write_text_proto(
    (output_uri / log_union_filename).string(), file_header, make_union_result.value());
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
