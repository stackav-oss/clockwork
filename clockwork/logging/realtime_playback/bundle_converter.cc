// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/realtime_playback/bundle_converter.hh"

#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/realtime_playback/converter_setup.hh"
#include "clockwork/logging/realtime_playback/initialization_converter.hh"
#include "clockwork/logging/realtime_playback/realtime_playback_conversion_config_clk_cc.hh"
#include "clockwork/logging/realtime_playback/realtime_playback_stream_kind_clk_cc.hh"
#include "clockwork/logging/realtime_playback/regular_converter.hh"
#include "clockwork/logging/realtime_playback/v1/realtime_playback_converter_manifest.pb.h"
#include "clockwork/logging/xxh3_checksum.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/container/compare.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/scope_guard/scope_guard.hh"
#include "jewels/std/span.hh"
#include "jewels/uuid/uuid.hh"

#include <google/protobuf/text_format.h>
#include <xxh3.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace clockwork_logging::realtime_playback
{
namespace
{

namespace manifest_v1 = clockwork::logging::realtime_playback::v1;

constexpr std::string_view manifest_filename = "converter_manifest.textproto";
constexpr size_t checksum_buffer_size = 64U * size_t{1024U};

struct NodeAccumulator
{
  std::optional<std::string> regular_log_path;
  std::optional<std::string> camera_log_path;
  std::map<ProcessUuid, std::optional<std::string>> processes;
  std::map<std::string, CameraChannelResult> camera_channels;
};

[[nodiscard]] bool path_entry_exists(const std::filesystem::path& path)
{
  std::error_code error;
  const auto status = std::filesystem::symlink_status(path, error);
  return error != std::errc::no_such_file_or_directory && status.type() != std::filesystem::file_type::not_found;
}

jewels::BinaryOutcome
checksum_file(jewels::Out<uint64_t> size_out, jewels::Out<uint64_t> checksum_out, const std::filesystem::path& path)
{
  std::ifstream stream{path, std::ios::binary};
  if (!stream)
  {
    return jewels::failure;
  }
  auto state = init_xxh3_checksum();
  std::vector<char> buffer(checksum_buffer_size);
  uint64_t size{};
  while (true)
  {
    stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto read_size = stream.gcount();
    if (read_size > 0)
    {
      const auto chunk_size = static_cast<size_t>(read_size);
      update_xxh3_checksum(state, std::as_bytes(std::span{buffer}.first(chunk_size)));
      size += chunk_size;
    }
    if (stream.eof())
    {
      break;
    }
    if (!stream)
    {
      return jewels::failure;
    }
  }
  *size_out = size;
  *checksum_out = digest_xxh3_checksum(state);
  return jewels::success;
}

jewels::BinaryOutcome write_file(const std::filesystem::path& path, const std::string_view contents)
{
  std::ofstream stream{path, std::ios::binary | std::ios::trunc};
  if (!stream)
  {
    return jewels::failure;
  }
  stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  stream.close();
  return stream ? jewels::success : jewels::failure;
}

jewels::BinaryOutcome
collect_files(jewels::Out<std::vector<std::filesystem::path>> paths_out, const std::filesystem::path& root_path)
{
  std::vector<std::filesystem::path> paths;
  std::error_code error;
  for (std::filesystem::recursive_directory_iterator iter{root_path, error}, end; iter != end; iter.increment(error))
  {
    if (error)
    {
      return jewels::failure;
    }
    if (iter->is_regular_file(error))
    {
      paths.emplace_back(iter->path().lexically_relative(root_path));
    }
    if (error)
    {
      return jewels::failure;
    }
  }
  if (error)
  {
    return jewels::failure;
  }
  std::ranges::sort(paths);
  *paths_out = std::move(paths);
  return jewels::success;
}

jewels::BinaryOutcome accumulate_nodes(
  jewels::InOut<std::map<std::string, NodeAccumulator>> nodes,
  const ConverterSetup& setup,
  const RegularConversionResult& regular_result,
  const CameraConversionResult& camera_result,
  const InitializationConversionResult& initialization_result)
{
  for (const auto& node : setup.nodes)
  {
    nodes->try_emplace(node.simplelaunch_node_name);
  }
  for (const auto& process : setup.initialization_processes)
  {
    nodes->try_emplace(process.simplelaunch_node_name);
    nodes->at(process.simplelaunch_node_name).processes.try_emplace(process.process_uuid);
  }
  for (const auto& regular_log : regular_result.regular_logs)
  {
    const auto node_iter = nodes->find(regular_log.simplelaunch_node_name);
    if (node_iter == nodes->end())
    {
      jewels::log_cerr_error(
        "Regular conversion returned unknown SimpleLaunch node: {}", regular_log.simplelaunch_node_name);
      return jewels::failure;
    }
    node_iter->second.regular_log_path = regular_log.relative_path;
  }
  for (const auto& camera_node : camera_result.nodes)
  {
    const auto node_iter = nodes->find(camera_node.simplelaunch_node_name);
    if (node_iter == nodes->end())
    {
      jewels::log_cerr_error(
        "Camera conversion returned unknown SimpleLaunch node: {}", camera_node.simplelaunch_node_name);
      return jewels::failure;
    }
    auto& node = node_iter->second;
    node.camera_log_path = camera_node.relative_path;
    for (const auto& channel : camera_node.channels)
    {
      node.camera_channels.emplace(channel.destination_channel_name, channel);
    }
  }
  for (const auto& initialization_log : initialization_result.initialization_logs)
  {
    const auto node_iter = nodes->find(initialization_log.simplelaunch_node_name);
    if (node_iter == nodes->end())
    {
      jewels::log_cerr_error(
        "Initialization conversion returned unknown SimpleLaunch node: {}", initialization_log.simplelaunch_node_name);
      return jewels::failure;
    }
    const auto process_iter = node_iter->second.processes.find(initialization_log.process_uuid);
    if (process_iter == node_iter->second.processes.end())
    {
      jewels::log_cerr_error(
        "Initialization conversion returned an unplanned process: {}", initialization_log.process_uuid.to_string());
      return jewels::failure;
    }
    process_iter->second = initialization_log.relative_path;
  }
  return jewels::success;
}

void add_nodes_to_manifest(
  jewels::InOut<manifest_v1::RealtimePlaybackConverterManifest> manifest,
  const std::map<std::string, NodeAccumulator>& nodes)
{
  for (const auto& [node_name, node] : nodes)
  {
    auto* node_result = manifest->add_nodes();
    node_result->set_simplelaunch_node_name(node_name);
    if (node.regular_log_path)
    {
      node_result->set_regular_log_path(*node.regular_log_path);
    }
    if (node.camera_log_path)
    {
      node_result->set_camera_log_path(*node.camera_log_path);
    }
    for (const auto& [process_uuid, initialization_log_path] : node.processes)
    {
      auto* process_result = node_result->add_processes();
      const auto process_uuid_chars = jewels::as_chars(std::span{process_uuid.uuid});
      process_result->set_process_uuid(process_uuid_chars.data(), process_uuid_chars.size());
      if (initialization_log_path)
      {
        process_result->set_initialization_log_path(*initialization_log_path);
      }
    }
    for (const auto& [destination_channel_name, camera_channel] : node.camera_channels)
    {
      auto* channel_result = node_result->add_camera_channels();
      channel_result->set_destination_channel_name(destination_channel_name);
      if (camera_channel.idr_time)
      {
        channel_result->set_idr_time_ns(camera_channel.idr_time->get_nanoseconds());
      }
      channel_result->set_preroll_message_count(camera_channel.preroll_message_count);
    }
  }
}

jewels::BinaryOutcome add_files_to_manifest(
  jewels::InOut<manifest_v1::RealtimePlaybackConverterManifest> manifest, const std::filesystem::path& output_path)
{
  std::vector<std::filesystem::path> relative_paths;
  if (jewels::fails(collect_files(jewels::Out{relative_paths}, output_path)))
  {
    return jewels::failure;
  }
  uint64_t total_size{};
  for (const auto& relative_path : relative_paths)
  {
    if (relative_path == manifest_filename)
    {
      return jewels::failure;
    }
    uint64_t size{};
    uint64_t checksum{};
    if (jewels::fails(checksum_file(jewels::Out{size}, jewels::Out{checksum}, output_path / relative_path)))
    {
      return jewels::failure;
    }
    auto* file = manifest->add_files();
    file->set_relative_path(relative_path.generic_string());
    file->set_size_bytes(size);
    file->set_xxh3(checksum);
    total_size += size;
  }
  manifest->set_total_artifact_size_bytes(total_size);
  return jewels::success;
}

[[nodiscard]] bool has_camera_assignments(const ConverterSetup& setup)
{
  return std::ranges::any_of(
    setup.assignments,
    [](const ConverterAssignment& assignment) { return assignment.stream_kind == RealtimePlaybackStreamKind::camera; });
}

} // namespace

jewels::BinaryOutcome convert_realtime_playback_bundle(
  jewels::Out<BundleConversionResult> result_out,
  const ConverterRequest& request,
  const CameraConverterFunction& camera_converter)
{
  const std::filesystem::path output_path{request.output_path};
  const auto parent_path = output_path.parent_path().empty() ? std::filesystem::path{"."} : output_path.parent_path();
  std::error_code error;
  if (
    output_path.filename().empty() || path_entry_exists(output_path) ||
    !std::filesystem::is_directory(parent_path, error) || error)
  {
    jewels::log_cerr_error("Output must be a new path beneath an existing directory: {}", request.output_path);
    return jewels::failure;
  }
  if (!std::filesystem::create_directory(output_path, error) || error)
  {
    jewels::log_cerr_error("Failed to create bundle output directory: {}", output_path.string());
    return jewels::failure;
  }
  bool conversion_succeeded = false;
  jewels::ScopeGuard failure_reporter{
    [&output_path, &conversion_succeeded]() noexcept
    {
      if (!conversion_succeeded)
      {
        jewels::log_cerr_error("Bundle conversion failed; output may be incomplete: {}", output_path.string());
      }
    }};

  std::shared_ptr<const ConversionConfig> config;
  if (jewels::fails(load_conversion_config(jewels::Out{config}, request.generated_config_path)))
  {
    return jewels::failure;
  }
  ConverterContext context;
  if (jewels::fails(prepare_converter_context(jewels::Out{context}, request, *config)))
  {
    return jewels::failure;
  }
  const auto convert_camera_streams = has_camera_assignments(context.setup);
  if (convert_camera_streams && !camera_converter)
  {
    jewels::log_cerr_error("Camera assignments require a camera converter implementation");
    return jewels::failure;
  }
  RegularConversionResult regular_result;
  CameraConversionResult camera_result;
  InitializationConversionResult initialization_result;
  jewels::log_cerr_info("Converting regular logs");
  if (jewels::fails(convert_regular_logs(jewels::Out{regular_result}, request, context)))
  {
    return jewels::failure;
  }
  if (convert_camera_streams)
  {
    jewels::log_cerr_info("Converting camera logs");
    if (jewels::fails(camera_converter(camera_result, request, context)))
    {
      return jewels::failure;
    }
  }
  jewels::log_cerr_info("Writing initialization logs");
  if (jewels::fails(convert_initialization_logs(jewels::Out{initialization_result}, request, context)))
  {
    return jewels::failure;
  }

  manifest_v1::RealtimePlaybackConverterManifest manifest;
  manifest.set_format_version(config->get_format_version());
  manifest.set_platform_id(config->get_platform_id());
  manifest.set_platform_config_xxh3(config->get_platform_config_xxh3());
  manifest.set_requested_start_time_ns(context.requested_interval.get_start_timestamp().get_nanoseconds());
  manifest.set_requested_end_time_ns(context.requested_interval.get_end_timestamp().get_nanoseconds());
  std::map<std::string, NodeAccumulator> nodes;
  if (jewels::fails(
        accumulate_nodes(jewels::InOut{nodes}, context.setup, regular_result, camera_result, initialization_result)))
  {
    return jewels::failure;
  }
  add_nodes_to_manifest(jewels::InOut{manifest}, nodes);
  jewels::log_cerr_info("Verifying bundle artifacts");
  if (jewels::fails(add_files_to_manifest(jewels::InOut{manifest}, output_path)))
  {
    return jewels::failure;
  }
  std::string manifest_text;
  if (
    !google::protobuf::TextFormat::PrintToString(manifest, &manifest_text) ||
    jewels::fails(write_file(output_path / manifest_filename, manifest_text)))
  {
    return jewels::failure;
  }
  conversion_succeeded = true;
  *result_out = BundleConversionResult{
    .requested_interval = context.requested_interval,
    .output_path = output_path.string(),
    .artifact_file_count = static_cast<size_t>(manifest.files_size()),
    .total_artifact_size_bytes = manifest.total_artifact_size_bytes()};
  return jewels::success;
}

} // namespace clockwork_logging::realtime_playback
