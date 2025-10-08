// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/offboard/merge_logs.hh"

#include "clockwork/logging/log_error.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/filesystem.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"

#include <tclap/CmdLine.h>
#include <tclap/MultiArg.h>
#include <tclap/ValueArg.h>

#include <chrono>
#include <cstdint>
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
/// @param[in] writer_config_path Writer config textproto file path
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

int32_t main(int32_t argc, char* argv[])
{
  try
  {
    TCLAP::CmdLine cmd("Merge logs", ' ', "1.0", true);
    const TCLAP::MultiArg<std::string> topic_arg("t", "topic", "Topic", false, "name", cmd);
    const TCLAP::ValueArg<int64_t> start_offset_arg(
      "s", "start-offset", "Start time relative offset ", false, 0, "seconds", cmd);
    const TCLAP::ValueArg<int64_t> end_offset_arg(
      "e", "end-offset", "End time relative offset ", false, 0, "seconds", cmd);
    const TCLAP::ValueArg<std::string> writer_config_path_arg(
      "w", "writer-config-path", "Writer config path", false, "", "path", cmd);
    const TCLAP::MultiArg<std::string> input_uris_arg("i", "input", "Input log URI ", true, "uri", cmd);
    const TCLAP::ValueArg<std::string> output_uri_arg("o", "output", "Output log URI", true, "", "uri", cmd);

    cmd.parse(argc, argv);

    const auto& input_uris = input_uris_arg.getValue();
    const auto& output_uri = output_uri_arg.getValue();
    const auto& writer_config_path = writer_config_path_arg.getValue();
    const auto& topics = topic_arg.getValue();
    const auto& start_offset_s = start_offset_arg.getValue();
    const auto& end_offset_s = end_offset_arg.getValue();

    const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};

    std::optional<clockwork_logging::RelativeInterval> maybe_log_interval;
    if (start_offset_s != 0 || end_offset_s != 0)
    {
      maybe_log_interval.emplace(
        clockwork_logging::RelativeInterval{
          .start_offset = std::chrono::seconds(start_offset_s),
        });
      if (end_offset_s != 0)
      {
        maybe_log_interval->end_offset = std::chrono::seconds(end_offset_s);
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

    std::pmr::string writer_config_str{memory_resource};
    if (!writer_config_path.empty())
    {
      auto load_result = clockwork_logging::offboard::load_config_proto(memory_resource, writer_config_path);
      if (!load_result)
      {
        jewels::log_cerr_error("Failed to load writer config from {}: {}", writer_config_path, load_result.error());
        return 1;
      }
      writer_config_str = std::move(load_result).value();
    }

    std::vector<std::string_view> input_uri_views{input_uris.begin(), input_uris.end()};
    if (const auto merge_result = clockwork_logging::offboard::merge_logs(
          memory_resource, input_uri_views, output_uri, maybe_desired_channels, maybe_log_interval, writer_config_str);
        !merge_result)
    {
      jewels::log_cerr_error("Failed to merge logs: {}", merge_result.error());
      return 1;
    }
    std::cout << "Success:\n";
    return 0;
  }
  catch (const std::exception& exc)
  {
    std::cerr << "Caught unexpected exception: " << exc.what() << '\n';
    return 1;
  }
}
