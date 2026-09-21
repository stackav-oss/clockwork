// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/realtime_playback/converter_request.hh"
#include "clockwork/logging/realtime_playback/converter_setup.hh"
#include "clockwork/logging/realtime_playback/converter_writer.hh"
#include "clockwork/logging/realtime_playback/initialization_converter.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/container/compare.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/scope_guard/scope_guard.hh"
#include "jewels/shared_pool/ref_counted_pool.hh"
#include "jewels/uuid/uuid.hh"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

namespace clockwork_logging::realtime_playback
{
namespace
{

struct ProcessWriter
{
  std::string simplelaunch_node_name;
  std::string relative_path;
  std::unique_ptr<ConverterWriter> writer;
};

using ProcessWriters = std::map<ProcessUuid, ProcessWriter>;

jewels::BinaryOutcome
validate_initialization_selection(const ConverterSetup& setup, const InitializationSelection& selection)
{
  for (const auto& requirement : setup.initialization_requirements)
  {
    if (!selection.contains(requirement.source_channel_name) && !requirement.allow_missing)
    {
      jewels::log_cerr_error("No logged initialization value and no fallback: {}", requirement.source_channel_name);
      return jewels::failure;
    }
  }
  return jewels::success;
}

jewels::BinaryOutcome open_initialization_writers(
  jewels::Out<ProcessWriters> writers_out,
  const ConverterRequest& request,
  const ConverterSetup& setup,
  const InitializationSelection& selection)
{
  ProcessWriters writers;
  jewels::ScopeGuard cleanup{[&writers]() noexcept { cleanup_converter_writers(jewels::InOut{writers}); }};
  for (const auto& process : setup.initialization_processes)
  {
    if (!std::ranges::any_of(
          process.requirement_indices,
          [&setup, &selection](const size_t index)
          { return selection.contains(setup.initialization_requirements.at(index).source_channel_name); }))
    {
      continue;
    }
    const auto relative_path = (std::filesystem::path{"nodes"} / process.simplelaunch_node_name / "initialization" /
                                process.process_uuid.to_string())
                                 .string();
    const auto absolute_path = std::filesystem::path{request.output_path} / relative_path;
    std::error_code error;
    std::filesystem::create_directories(absolute_path, error);
    if (error)
    {
      jewels::log_cerr_error(
        "Failed to create initialization log directory {}: {}", absolute_path.string(), error.message());
      return jewels::failure;
    }
    std::unique_ptr<ConverterWriter> writer;
    if (jewels::fails(open_converter_writer(jewels::Out{writer}, absolute_path.string(), "initialization")))
    {
      return jewels::failure;
    }
    jewels::ScopeGuard writer_cleanup{[&writer]() noexcept
                                      {
                                        if (writer)
                                        {
                                          // Cleanup is best effort while unwinding a failed conversion.
                                          std::ignore = close_converter_writer(jewels::InOut{*writer});
                                        }
                                      }};
    for (const auto index : process.requirement_indices)
    {
      const auto& requirement = setup.initialization_requirements.at(index);
      if (!selection.contains(requirement.source_channel_name))
      {
        continue;
      }
      if (jewels::fails(add_converter_channel(jewels::InOut{*writer}, requirement)))
      {
        return jewels::failure;
      }
    }
    writers.emplace(
      process.process_uuid,
      ProcessWriter{
        .simplelaunch_node_name = process.simplelaunch_node_name,
        .relative_path = relative_path,
        .writer = std::move(writer)});
    writer_cleanup.dismiss();
  }
  *writers_out = std::move(writers);
  cleanup.dismiss();
  return jewels::success;
}

jewels::BinaryOutcome write_initialization_messages(
  ProcessWriters& writers, const ConverterSetup& setup, const InitializationSelection& selection)
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  ConverterMessageWriter message_writer{memory_resource};
  for (const auto& process : setup.initialization_processes)
  {
    const auto writer_iter = writers.find(process.process_uuid);
    if (writer_iter == writers.end())
    {
      continue;
    }
    for (const auto index : process.requirement_indices)
    {
      const auto& requirement = setup.initialization_requirements.at(index);
      const auto selected_iter = selection.find(requirement.source_channel_name);
      if (selected_iter == selection.end())
      {
        continue;
      }
      const auto& selected = selected_iter->second;
      if (jewels::fails(message_writer.write(
            jewels::InOut{*writer_iter->second.writer},
            requirement,
            ConverterMessageView{
              .sequence_number = selected.sequence_number,
              .publish_time = selected.publish_time,
              .log_time = selected.log_time,
              .header = selected.header,
              .data = selected.data})))
      {
        jewels::log_cerr_error("Failed to write initialization message: {}", requirement.destination_channel_name);
        return jewels::failure;
      }
    }
  }
  return jewels::success;
}

} // namespace

jewels::BinaryOutcome convert_initialization_logs(
  jewels::Out<InitializationConversionResult> result_out,
  const ConverterRequest& request,
  const ConverterContext& context)
{
  const auto& setup = context.setup;
  std::vector<std::string_view> source_channel_names;
  for (const auto& requirement : setup.initialization_requirements)
  {
    if (requirement.upgrader)
    {
      source_channel_names.emplace_back(requirement.source_channel_name);
    }
  }
  InitializationSelection selection;
  if (jewels::fails(select_initialization_messages(
        jewels::Out{selection},
        request.source_uri,
        LogInterval{context.source_interval.get_start_timestamp(), context.requested_interval.get_start_timestamp()},
        source_channel_names)))
  {
    return jewels::failure;
  }
  if (jewels::fails(validate_initialization_selection(setup, selection)))
  {
    return jewels::failure;
  }
  ProcessWriters writers;
  if (jewels::fails(open_initialization_writers(jewels::Out{writers}, request, setup, selection)))
  {
    return jewels::failure;
  }
  jewels::ScopeGuard writers_cleanup{[&writers]() noexcept { cleanup_converter_writers(jewels::InOut{writers}); }};
  if (jewels::fails(write_initialization_messages(writers, setup, selection)))
  {
    return jewels::failure;
  }
  if (jewels::fails(finalize_converter_writers(jewels::InOut{writers}, "initialization")))
  {
    return jewels::failure;
  }

  InitializationConversionResult result;
  for (const auto& [process_uuid, process_writer] : writers)
  {
    result.initialization_logs.emplace_back(
      InitializationLogResult{
        .process_uuid = process_uuid,
        .simplelaunch_node_name = process_writer.simplelaunch_node_name,
        .relative_path = process_writer.relative_path});
  }
  *result_out = std::move(result);
  return jewels::success;
}

} // namespace clockwork_logging::realtime_playback
