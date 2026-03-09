// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/decompress_option.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/readers/abstract_log_reader.hh"
#include "clockwork/logging/readers/log_reader_factory.hh"
#include "clockwork/logging/readers/types.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"

#include <fmt/base.h>
#include <tclap/CmdLine.h>
#include <tclap/MultiArg.h>
#include <tclap/UnlabeledValueArg.h>
#include <tclap/ValueArg.h>

#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace clockwork_logging::offboard
{

/// Open the source log reader
/// @param[in] memory_resource Memory resource
/// @param[in] log_uri Log URI
/// @param[in] maybe_desired_channels Optional set of desired channels
/// @param[in] maybe_log_interval Optional relative log interval
/// @return Reader pointer or LogError on failure
[[nodiscard]] LogExpected<std::unique_ptr<AbstractLogReader>> open_reader(
  jewels::memory::MemoryResource memory_resource,
  std::string_view log_uri,
  const std::optional<std::pmr::unordered_set<std::pmr::string>>& maybe_desired_channels,
  const std::optional<RelativeInterval>& maybe_log_interval)
{
  const auto topic_filter = [memory_resource, maybe_desired_channels](std::string_view topic)
  { return !maybe_desired_channels || maybe_desired_channels->contains(std::pmr::string{topic, memory_resource}); };
  try
  {
    auto reader_ptr = make_reader(log_uri, {}, maybe_log_interval, DecompressOption::dont_decompress);
    if (const auto open_result = reader_ptr->open(topic_filter); !open_result)
    {
      return jewels::unexpected(open_result.error());
    }
    return {std::move(reader_ptr)};
  }
  catch (const std::invalid_argument& exc)
  {
    jewels::log_cerr_error("{}", exc.what());
    return jewels::unexpected(LogError::failed_to_open_log_file);
  }
  __builtin_unreachable();
}

/// Dump the contents of the log
/// @param[in] memory_resource Memory resource
/// @param[in] log_uri Source log URI
/// @param[in] maybe_desired_channels Optional set of desired channels
/// @param[in] maybe_log_interval Optional relative log interval
/// @return LogError on failure
[[nodiscard]] LogExpected<void> cat_log(
  jewels::memory::MemoryResource memory_resource,
  std::string_view log_uri,
  const std::optional<std::pmr::unordered_set<std::pmr::string>>& maybe_desired_channels,
  const std::optional<RelativeInterval>& maybe_log_interval)
{
  const auto reader_result = open_reader(memory_resource, log_uri, maybe_desired_channels, maybe_log_interval);
  if (!reader_result)
  {
    jewels::log_cerr_error("Failed to open {} for read: {}", log_uri, reader_result.error());
    return jewels::unexpected(reader_result.error());
  }
  auto& reader = *reader_result.value();
  const auto log_metadata = reader.get_metadata();
  std::unordered_map<std::string_view, jewels::memory::ObjectPtr<const TopicMetadata>> topic_map;
  for (const auto& metadata : log_metadata)
  {
    topic_map.emplace(metadata.name, jewels::memory::make_non_null_from_ref(metadata));
  }
  while (true)
  {
    const auto maybe_message = reader.next_message();
    if (!maybe_message)
    {
      break;
    }
    std::string_view message_type = "UNKNOWN TYPE";
    const auto topic_iter = topic_map.find(maybe_message->topic);
    if (topic_iter != topic_map.end())
    {
      message_type = topic_iter->second->type;
    }
    fmt::print(
      "{:.9f}[{:8d}] {} [{}] {}/{} bytes\n",
      std::chrono::duration<double>(std::chrono::nanoseconds(maybe_message->publish_time.get_nanoseconds())).count(),
      maybe_message->sequence_number,
      maybe_message->topic,
      message_type,
      maybe_message->header.size(),
      maybe_message->data.size());
  }
  return {};
}

} // namespace clockwork_logging::offboard

int main(int32_t argc, char* argv[])
{
  try
  {
    TCLAP::CmdLine cmd("Cat log", ' ', "1.0", true);
    const TCLAP::MultiArg<std::string> topic_arg("t", "topic", "Topic ", false, "name", cmd);
    const TCLAP::ValueArg<int64_t> start_offset_arg(
      "s", "start-offset", "Start time relative offset ", false, 0, "seconds", cmd);
    const TCLAP::ValueArg<int64_t> end_offset_arg(
      "e", "end-offset", "End time relative offset ", false, 0, "seconds", cmd);
    const TCLAP::UnlabeledValueArg<std::string> log_uri_arg("uri", "Log URI", true, "", "uri", cmd);

    cmd.parse(argc, argv);

    const auto& log_uri = log_uri_arg.getValue();
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

    if (const auto cat_result =
          clockwork_logging::offboard::cat_log(memory_resource, log_uri, maybe_desired_channels, maybe_log_interval);
        !cat_result)
    {
      jewels::log_cerr_error("Failed to cat log: {}", cat_result.error());
      return 1;
    }
    return 0;
  }
  catch (const std::exception& exc)
  {
    std::cerr << "Caught unexpected exception: " << exc.what() << '\n';
    return 1;
  }
}
