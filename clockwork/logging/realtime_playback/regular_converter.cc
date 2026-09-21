// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/realtime_playback/regular_converter.hh"

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/readers/abstract_log_reader.hh"
#include "clockwork/logging/readers/log_reader_factory.hh"
#include "clockwork/logging/readers/types.hh"
#include "clockwork/logging/realtime_playback/converter_writer.hh"
#include "clockwork/logging/realtime_playback/realtime_playback_stream_kind_clk_cc.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/scope_guard/scope_guard.hh"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <memory_resource>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace clockwork_logging::realtime_playback
{
jewels::BinaryOutcome convert_regular_logs(
  jewels::Out<RegularConversionResult> result_out, const ConverterRequest& request, const ConverterContext& context)
{
  const auto& setup = context.setup;
  const auto is_regular = [&setup](const size_t index)
  { return setup.assignments.at(index).stream_kind == RealtimePlaybackStreamKind::regular; };
  ConverterNodeWriters writers;
  if (jewels::fails(open_converter_node_writers(
        jewels::Out{writers},
        request,
        setup,
        "regular",
        [&is_regular](const ConverterNode& node) { return std::ranges::any_of(node.assignment_indices, is_regular); },
        is_regular)))
  {
    return jewels::failure;
  }
  jewels::ScopeGuard writers_cleanup{[&writers]() noexcept { cleanup_converter_writers(jewels::InOut{writers}); }};
  std::map<std::string_view, std::vector<size_t>> assignments_by_source;
  for (const auto index : std::ranges::views::iota(size_t{0}, setup.assignments.size()))
  {
    const auto& assignment = setup.assignments.at(index);
    if (assignment.stream_kind == RealtimePlaybackStreamKind::regular)
    {
      assignments_by_source[assignment.source_channel_name].emplace_back(index);
    }
  }
  auto reader = make_reader(request.source_uri, context.requested_interval, {});
  if (!reader->open([&assignments_by_source](const auto topic) { return assignments_by_source.contains(topic); }))
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
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  ConverterMessageWriter message_writer{memory_resource};
  while (const auto message = reader->next_message())
  {
    for (const auto index : assignments_by_source.at(message->topic))
    {
      const auto& assignment = setup.assignments.at(index);
      if (jewels::fails(message_writer.write(
            jewels::InOut{*writers.at(assignment.simplelaunch_node_name).writer},
            assignment,
            ConverterMessageView{
              .sequence_number = message->sequence_number,
              .publish_time = message->publish_time,
              .log_time = message->log_time,
              .header = message->header,
              .data = message->data})))
      {
        return jewels::failure;
      }
    }
  }
  const auto reader_close_result = reader->close();
  reader_open = false;
  const auto writers_close_result = finalize_converter_writers(jewels::InOut{writers}, "regular");
  if (!reader_close_result || jewels::fails(writers_close_result))
  {
    return jewels::failure;
  }
  RegularConversionResult result;
  for (const auto& [node_name, node_writer] : writers)
  {
    result.regular_logs.emplace_back(
      RegularLogResult{.simplelaunch_node_name = node_name, .relative_path = node_writer.relative_path});
  }
  *result_out = std::move(result);
  return jewels::success;
}

} // namespace clockwork_logging::realtime_playback
