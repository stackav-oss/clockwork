// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/decompress_option.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/readers/abstract_log_reader.hh"
#include "clockwork/logging/readers/log_reader_factory.hh"
#include "clockwork/logging/readers/types.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/log_cerr/log_cerr.hh"

#include <fmt/base.h>
#include <fmt/ranges.h>
#include <tclap/CmdLine.h>
#include <tclap/MultiArg.h>
#include <tclap/UnlabeledValueArg.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace clockwork_logging
{

/// Dump logged messages in hex
/// @param[in] log_uri Source log URI
/// @param[in] topic Topic to dump
/// @param[in] sequence_numbers Sequence numbers to dump
/// @return LogError on failure
LogOutcome
hex_dump(std::string_view log_uri, std::string_view topic, const std::unordered_set<uint32_t>& sequence_numbers)
{
  const auto reader = make_reader(log_uri, {}, {}, DecompressOption::dont_decompress);
  if (const auto open_result =
        reader->open([desired_topic = topic](std::string_view topic) { return topic == desired_topic; });
      !open_result)
  {
    return open_result.error();
  }
  while (true)
  {
    const auto maybe_message = reader->next_message();
    if (!maybe_message)
    {
      break;
    }
    if (sequence_numbers.empty() || sequence_numbers.contains(maybe_message->sequence_number))
    {
      fmt::println(
        "\n{:.9f}[{:8d}] {} {}",
        std::chrono::duration<double>(std::chrono::nanoseconds(maybe_message->publish_time.get_nanoseconds())).count(),
        maybe_message->sequence_number,
        maybe_message->topic,
        maybe_message->is_lite_compressed ? "compressed" : "not-compressed");
      fmt::println("{:02X}", fmt::join(maybe_message->data, ""));
    }
  }
  return LogError::success;
}

} // namespace clockwork_logging

int main(int argc, char* argv[])
{
  using jewels::fails;

  try
  {
    TCLAP::CmdLine cmd("Cat log", ' ', "1.0", true);
    const TCLAP::MultiArg<uint32_t> sequence_number_arg(
      "s", "sequence-number", "Sequence number", false, "number", cmd);
    const TCLAP::UnlabeledValueArg<std::string> topic_arg("topic", "Topic", true, "", "string", cmd);
    const TCLAP::UnlabeledValueArg<std::string> log_uri_arg("uri", "Log URI", true, "", "uri", cmd);

    cmd.parse(argc, argv);

    const std::unordered_set<uint32_t> sequence_numbers(
      sequence_number_arg.getValue().begin(), sequence_number_arg.getValue().end());
    const auto& topic = topic_arg.getValue();
    const auto& log_uri = log_uri_arg.getValue();

    if (const auto dump_outcome = clockwork_logging::hex_dump(log_uri, topic, sequence_numbers); fails(dump_outcome))
    {
      jewels::log_cerr_error("Failed to cat log: {}", dump_outcome.get());
      return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
  }
  catch (const std::exception& exc)
  {
    std::cerr << "Caught unexpected exception: " << exc.what() << '\n';
    return EXIT_FAILURE;
  }
}
