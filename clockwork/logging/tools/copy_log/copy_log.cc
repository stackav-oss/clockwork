// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/offboard/copy_log.hh"

#include "clockwork/logging/log_error.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/filesystem.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/otel/otel.hh"
#include "jewels/std/expected.hh"

#include <tclap/CmdLine.h>
#include <tclap/MultiArg.h>
#include <tclap/SwitchArg.h>
#include <tclap/UnlabeledValueArg.h>
#include <tclap/ValueArg.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <memory_resource>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace clockwork_logging::offboard
{

/// Load the writer config text proto from the specified file
/// @param[in] memory_resource Memory resource to use for allocations
/// @param[in] write_config_path Writer config textproto file path
/// @return Loaded config proto or ConfigError on failure
[[nodiscard]] jewels::expected<std::pmr::string, LogError>
load_config_proto(jewels::memory::MemoryResource memory_resource, std::string_view writer_config_path)
{
  jewels::log_cerr_info("Reading writer configuration from '{}'", writer_config_path);
  jewels::filesystem::Filesystem kits_fs{memory_resource};
  const auto open_result = kits_fs.open(writer_config_path);
  if (!open_result)
  {
    return jewels::unexpected(to_log_error(open_result.error()));
  }
  const auto& file_desc = open_result.value();
  const auto size_result = kits_fs.get_size(file_desc);
  if (!size_result)
  {
    return jewels::unexpected(to_log_error(size_result.error()));
  }
  std::pmr::string writer_config_str(size_result.value(), '\0', memory_resource);
  if (const auto read_result = kits_fs.read(file_desc, std::as_writable_bytes(std::span{writer_config_str}));
      !read_result)
  {
    return jewels::unexpected(to_log_error(read_result.error()));
  }
  return {std::move(writer_config_str)};
}

} // namespace clockwork_logging::offboard

int main(int32_t argc, char* argv[])
{
  try
  {
    jewels::otel::set_up_trace_provider("copy_log");

    TCLAP::CmdLine cmd("Copy log", ' ', "1.0", true);
    const TCLAP::MultiArg<std::string> topic_arg("t", "topic", "Topic to copy", false, "name", cmd);
    const TCLAP::MultiArg<std::string> excluded_topic_arg(
      "x", "exclude", "Topic to exclude from the copy", false, "name", cmd);
    const TCLAP::ValueArg<double> start_offset_arg(
      "s", "start-offset", "Start time relative offset ", false, 0, "seconds", cmd);
    const TCLAP::ValueArg<double> end_offset_arg(
      "e", "end-offset", "End time relative offset ", false, 0, "seconds", cmd);
    const TCLAP::ValueArg<std::string> writer_config_path_arg(
      "w", "writer-config-path", "Writer config path", false, "", "path", cmd);
    const TCLAP::UnlabeledValueArg<std::string> source_uri_arg("source", "Source log URI", true, "", "uri", cmd);
    const TCLAP::UnlabeledValueArg<std::string> dest_uri_arg(
      "destination", "Destination log URI", true, "", "uri", cmd);
    const TCLAP::SwitchArg no_deep_copy_arg(
      "n", "no-deep-copy-log-unions", "Just copy the union metadata file for overlapping logs", cmd);

    cmd.parse(argc, argv);

    const auto& source_uri = source_uri_arg.getValue();
    const auto& dest_uri = dest_uri_arg.getValue();
    const auto& writer_config_path = writer_config_path_arg.getValue();
    const auto& topics = topic_arg.getValue();
    const auto& excluded_topics = excluded_topic_arg.getValue();
    const auto& start_offset_s = start_offset_arg.getValue();
    const auto& end_offset_s = end_offset_arg.getValue();
    const auto no_deep_copy = no_deep_copy_arg.getValue();

    const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};

    std::optional<clockwork_logging::RelativeInterval> maybe_log_interval;
    if (start_offset_s != 0 || end_offset_s != 0)
    {
      maybe_log_interval.emplace(
        clockwork_logging::RelativeInterval{
          .start_offset = std::chrono::round<std::chrono::nanoseconds>(std::chrono::duration<double>(start_offset_s)),
        });
      if (end_offset_s != 0)
      {
        maybe_log_interval->end_offset =
          std::chrono::round<std::chrono::nanoseconds>(std::chrono::duration<double>(end_offset_s));
      }
    }

    std::optional<std::pmr::unordered_set<std::pmr::string>> maybe_desired_channels;
    if (!topics.empty())
    {
      maybe_desired_channels.emplace(memory_resource);
      for (const auto& topic : topics)
      {
        maybe_desired_channels->insert(std::pmr::string{topic, memory_resource});
      }
    }

    std::optional<std::pmr::unordered_set<std::pmr::string>> maybe_excluded_channels;
    if (!excluded_topics.empty())
    {
      maybe_excluded_channels.emplace(memory_resource);
      for (const auto& excluded_topic : excluded_topics)
      {
        maybe_excluded_channels->insert(std::pmr::string{excluded_topic, memory_resource});
      }
    }

    std::pmr::string writer_config_str{memory_resource};
    if (!writer_config_path.empty())
    {
      auto load_result = clockwork_logging::offboard::load_config_proto(memory_resource, writer_config_path);
      if (!load_result)
      {
        jewels::log_cerr_error("Failed to load writer config from {}: {}", writer_config_path, load_result.error());
        return EXIT_FAILURE;
      }
      writer_config_str = std::move(load_result).value();
    }

    if (const auto copy_result = clockwork_logging::offboard::copy_log(
          /*memory_resource=*/memory_resource,
          /*source_uri=*/source_uri,
          /*dest_uri=*/dest_uri,
          /*maybe_desired_channels=*/maybe_desired_channels,
          /*maybe_excluded_channels=*/maybe_excluded_channels,
          /*maybe_log_interval=*/maybe_log_interval,
          /*writer_config_str=*/writer_config_str,
          /*no_deep_copy=*/no_deep_copy);
        !copy_result)
    {
      jewels::log_cerr_error("Failed to copy log: {}", copy_result.error());
      return EXIT_FAILURE;
    }
    std::cout << "Success\n";
    return EXIT_SUCCESS;
  }
  catch (const std::exception& exc)
  {
    std::cerr << "Caught unexpected exception: " << exc.what() << '\n';
    return EXIT_FAILURE;
  }
}
