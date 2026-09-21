// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/realtime_playback/initialization_converter.hh"

#include "clockwork/logging/readers/abstract_log_reader.hh"
#include "clockwork/logging/readers/log_reader_factory.hh"
#include "clockwork/logging/readers/types.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/scope_guard/scope_guard.hh"

#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace clockwork_logging::realtime_playback
{

jewels::BinaryOutcome select_initialization_messages(
  jewels::Out<InitializationSelection> selection_out,
  const std::string_view source_uri,
  const LogInterval interval,
  const std::span<const std::string_view> source_channel_names)
{
  std::set<std::string_view> requested_channels{source_channel_names.begin(), source_channel_names.end()};
  InitializationSelection selection;
  if (requested_channels.empty())
  {
    *selection_out = std::move(selection);
    return jewels::success;
  }

  auto reader = make_reader(source_uri, LogInterval{interval.get_start_timestamp()}, {});
  if (!reader->open([&requested_channels](const std::string_view topic) { return requested_channels.contains(topic); }))
  {
    return jewels::failure;
  }
  bool reader_open = true;
  jewels::ScopeGuard reader_cleanup{[&reader, &reader_open]() noexcept
                                    {
                                      if (reader_open)
                                      {
                                        // Cleanup is best effort while unwinding a failed conversion.
                                        std::ignore = reader->close();
                                      }
                                    }};
  // The initialization messages are persistent messages, so we can just take the first message for each requested
  // channel that is at or after the start of the interval.
  while (const auto message = reader->next_message())
  {
    const auto topic = std::string{message->topic};
    if (selection.contains(topic))
    {
      continue;
    }
    selection.emplace(
      topic,
      SelectedInitializationMessage{
        .sequence_number = message->sequence_number,
        .publish_time = message->publish_time,
        .log_time = message->log_time,
        .header = std::vector<std::byte>{message->header.begin(), message->header.end()},
        .data = std::vector<std::byte>{message->data.begin(), message->data.end()},
      });
    if (selection.size() == requested_channels.size())
    {
      break;
    }
  }
  const auto close_result = reader->close();
  reader_open = false;
  if (!close_result)
  {
    return jewels::failure;
  }
  *selection_out = std::move(selection);
  return jewels::success;
}

} // namespace clockwork_logging::realtime_playback
