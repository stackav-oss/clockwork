// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/offboard/chunk_reader_writer_factory.hh"
#include "clockwork/logging/offboard/log_format.hh"
#include "clockwork/logging/offboard/log_metadata_file_helper.hh"
#include "clockwork/logging/offboard/log_metadata_helper_interface.hh"
#include "clockwork/logging/offboard/log_union_file_helper.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"

#include <memory>
#include <memory_resource>
#include <string>

namespace clockwork_logging::offboard
{

[[nodiscard]] LogExpected<std::shared_ptr<LogMetadataHelperInterface>> make_log_metadata_helper(
  jewels::memory::MemoryResource memory_resource,
  const LogUri& log_uri,
  ChunkReaderWriterFactory<>& chunk_reader_factory)
{
  if (const auto exists_result = chunk_reader_factory.exists(log_uri.string());
      !exists_result || !exists_result.value())
  {
    if (exists_result)
    {
      jewels::log_cerr_error("Cannot access log under {}: {}", log_uri.string(), LogError::no_such_file_or_directory);
      return jewels::unexpected(LogError::no_such_file_or_directory);
    }
    jewels::log_cerr_error("Cannot access log under {}: {}", log_uri.string(), exists_result.error());
    return jewels::unexpected(exists_result.error());
  }
  const auto log_union_uri = log_uri / log_union_filename;
  if (const auto exists_result = chunk_reader_factory.exists(log_union_uri.string());
      exists_result && exists_result.value())
  {
    return make_log_union_file_helper(memory_resource, log_union_uri, chunk_reader_factory);
  }
  const auto log_metadata_uri = log_uri / log_metadata_filename;
  return make_log_metadata_file_helper(memory_resource, log_metadata_uri, chunk_reader_factory);
}

} // namespace clockwork_logging::offboard
