// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

// IWYU pragma: private, include "clockwork/journal/replay/execution_queue.hh"

#pragma once

#include "clockwork/journal/replay/execution_queue.hh"

#include "clockwork/journal/journal.pb.h"
#include "clockwork/journal/replay/execution_stats_clk_cc.hh"
#include "clockwork/journal/replay/message_buffer.hh"
#include "clockwork/journal/replay/replay_metadata.hh"
#include "clockwork/logging/readers/types.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/meta/types.hh"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace clockwork::journal::replay
{

template <ReplayChannel... Channels>
bool has_unique_channel_name(const std::string_view channel_name)
{
  return ((static_cast<uint32_t>(channel_name == std::string_view{Channels::name}) + ... + 0U) == 1U);
}

template <ReplayChannel... InputChannels, ReplayChannel... OutputChannels>
bool metadata_channels_match_configuration(
  jewels::meta::Types<InputChannels...> /*input_channels*/,
  jewels::meta::Types<OutputChannels...> /*output_channels*/,
  const ReplayMetadata& metadata)
{
  for (const auto& execution : metadata.executions)
  {
    const auto inputs_known = std::ranges::all_of(
      execution.inputs,
      [&execution](const ExecutionInput& input)
      {
        const auto& channel_name = input.journal_state.channel_name();
        return has_unique_channel_name<InputChannels...>(channel_name) &&
               std::ranges::count_if(
                 execution.inputs,
                 [&channel_name](const ExecutionInput& candidate)
                 { return candidate.journal_state.channel_name() == channel_name; }) == 1;
      });
    const auto outputs_known = std::ranges::all_of(
      execution.outputs,
      [&execution](const ExecutionOutput& output)
      {
        const auto& channel_name = output.journal_state.channel_name();
        return has_unique_channel_name<OutputChannels...>(channel_name) &&
               std::ranges::count_if(
                 execution.outputs,
                 [&channel_name](const ExecutionOutput& candidate)
                 { return candidate.journal_state.channel_name() == channel_name; }) == 1;
      });
    if (!inputs_known || !outputs_known)
    {
      return false;
    }
  }
  return true;
}

template <ReplayChannel Channel>
jewels::BinaryOutcome copy_inputs(
  jewels::Out<ReplayInput<Channel>> input,
  const ExecutionMetadata& execution,
  const MessageBuffer<typename Channel::MsgType>& buffer)
{
  input->messages.clear();
  const auto input_iter = std::ranges::find_if(
    execution.inputs,
    [](const ExecutionInput& input) { return input.journal_state.channel_name() == std::string_view{Channel::name}; });
  if (input_iter == execution.inputs.end())
  {
    input->journal_state.Clear();
    input->journal_state.set_channel_name(std::string{Channel::name});
    return jewels::success;
  }

  input->journal_state = input_iter->journal_state;
  input->messages.reserve(input_iter->messages.size());
  for (const auto& key : input_iter->messages)
  {
    auto& message = input->messages.emplace_back();
    if (jewels::fails(buffer.get(jewels::Out{message}, input_iter->journal_state.channel_name(), key)))
    {
      return jewels::failure;
    }
  }
  return jewels::success;
}

template <ReplayChannel Channel>
void copy_outputs(jewels::Out<ReplayOutput<Channel>> output, const ExecutionMetadata& execution)
{
  output->messages.clear();
  const auto output_iter = std::ranges::find_if(
    execution.outputs,
    [](const ExecutionOutput& output)
    { return output.journal_state.channel_name() == std::string_view{Channel::name}; });
  if (output_iter == execution.outputs.end())
  {
    output->journal_state.Clear();
    output->journal_state.set_channel_name(std::string{Channel::name});
    return;
  }
  output->journal_state = output_iter->journal_state;
  output->messages = output_iter->messages;
}

template <ReplayChannel Channel>
void refresh_missing_messages(
  jewels::Out<std::set<MessageKey>> missing_messages,
  const ExecutionMetadata& execution,
  const MessageBuffer<typename Channel::MsgType>& buffer)
{
  missing_messages->clear();
  const auto input_iter = std::ranges::find_if(
    execution.inputs,
    [](const ExecutionInput& input) { return input.journal_state.channel_name() == std::string_view{Channel::name}; });
  if (input_iter == execution.inputs.end())
  {
    // A configured input channel may be absent from an execution.
    return;
  }
  for (const auto& key : input_iter->messages)
  {
    if (!buffer.contains(input_iter->journal_state.channel_name(), key))
    {
      missing_messages->emplace(key);
    }
  }
}

template <ReplayChannel... InputChannels>
jewels::BinaryOutcome consume_execution_references(
  jewels::InOut<MessageBuffer<typename InputChannels::MsgType>>... buffers, const ExecutionMetadata& execution)
{
  for (const auto& input : execution.inputs)
  {
    const auto success = (
      [&]() -> bool
      {
        if (input.journal_state.channel_name() != std::string_view{InputChannels::name})
        {
          return false;
        }
        for (const auto& key : input.messages)
        {
          if (jewels::fails(buffers->consume(input.journal_state.channel_name(), key)))
          {
            return false;
          }
        }
        return true;
      }() ||
      ...);
    if (!success)
    {
      return jewels::failure;
    }
  }
  return jewels::success;
}

template <ReplayChannel... InputChannels, ReplayChannel... OutputChannels>
template <ReplayChannel Channel>
jewels::BinaryOutcome
ExecutionQueue<jewels::meta::Types<InputChannels...>, jewels::meta::Types<OutputChannels...>>::ChannelBuffer<Channel>::
  try_make(jewels::FactoryOut<ChannelBuffer> channel_buffer, const std::vector<ChannelRequirements>& requirements)
{
  auto message_buffer = jewels::FactoryResult<MessageBuffer<typename Channel::MsgType>>{};
  if (jewels::fails(
        MessageBuffer<typename Channel::MsgType>::try_make(jewels::FactoryOut{message_buffer}, requirements)))
  {
    return jewels::failure;
  }
  *channel_buffer = ChannelBuffer{std::move(*message_buffer)};
  return jewels::success;
}

template <ReplayChannel... InputChannels, ReplayChannel... OutputChannels>
template <ReplayChannel Channel>
ExecutionQueue<jewels::meta::Types<InputChannels...>, jewels::meta::Types<OutputChannels...>>::ChannelBuffer<
  Channel>::ChannelBuffer(MessageBuffer<typename Channel::MsgType> message_buffer)
  : buffer(std::move(message_buffer))
{
}

template <ReplayChannel... InputChannels, ReplayChannel... OutputChannels>
jewels::BinaryOutcome
ExecutionQueue<jewels::meta::Types<InputChannels...>, jewels::meta::Types<OutputChannels...>>::try_make(
  jewels::FactoryOut<ExecutionQueue> queue, const JournalFile& journal_file, const size_t cog_journal_index)
{
  ReplayMetadata metadata;
  if (jewels::fails(make_replay_metadata(jewels::Out{metadata}, journal_file, cog_journal_index)))
  {
    return jewels::failure;
  }
  if (!metadata_channels_match_configuration(InputChannelTypes{}, OutputChannelTypes{}, metadata))
  {
    return jewels::failure;
  }
  auto buffer_results = std::tuple<jewels::FactoryResult<ChannelBuffer<InputChannels>>...>{};
  if (!(jewels::ok(
          ChannelBuffer<InputChannels>::try_make(
            jewels::FactoryOut{std::get<jewels::FactoryResult<ChannelBuffer<InputChannels>>>(buffer_results)},
            metadata.input_channels)) &&
        ...))
  {
    return jewels::failure;
  }
  auto buffers =
    ChannelBuffers{std::move(*std::get<jewels::FactoryResult<ChannelBuffer<InputChannels>>>(buffer_results))...};
  *queue = ExecutionQueue{std::move(metadata), std::move(buffers)};
  return jewels::success;
}

template <ReplayChannel... InputChannels, ReplayChannel... OutputChannels>
ExecutionQueue<jewels::meta::Types<InputChannels...>, jewels::meta::Types<OutputChannels...>>::ExecutionQueue(
  ReplayMetadata metadata, ChannelBuffers buffers)
  : metadata_(std::move(metadata)), buffers_(std::move(buffers))
{
  stats_.set_total(metadata_.executions.size());
  refresh_current_execution_state();
}

template <ReplayChannel... InputChannels, ReplayChannel... OutputChannels>
template <ReplayChannel Channel>
  requires(std::same_as<Channel, InputChannels> || ...)
jewels::BinaryOutcome
ExecutionQueue<jewels::meta::Types<InputChannels...>, jewels::meta::Types<OutputChannels...>>::push(
  jewels::Out<std::vector<Execution>> executions,
  const clockwork_logging::LoggedMessage& logged_message,
  const typename Channel::MsgType& message)
{
  executions->clear();
  if (logged_message.topic != std::string_view{Channel::name})
  {
    return jewels::failure;
  }
  auto& buffer = std::get<ChannelBuffer<Channel>>(buffers_).buffer;
  static_cast<void>(buffer.push(logged_message, message));
  const auto pushed_key = static_cast<MessageKey>(logged_message.sequence_number);
  std::get<CurrentInputState<Channel>>(current_input_states_).missing_messages.erase(pushed_key);
  while (pushed_message_skips_next_execution<Channel>(pushed_key))
  {
    if (jewels::fails(discard_next_execution()))
    {
      return jewels::failure;
    }
    record_stream_drop();
  }
  return process_ready(jewels::Out{*executions});
}

template <ReplayChannel... InputChannels, ReplayChannel... OutputChannels>
jewels::BinaryOutcome
ExecutionQueue<jewels::meta::Types<InputChannels...>, jewels::meta::Types<OutputChannels...>>::process_ready(
  jewels::Out<std::vector<Execution>> executions)
{
  executions->clear();
  while (next_execution_is_ready())
  {
    if (jewels::fails(assemble_and_consume_next_execution(jewels::Out{executions->emplace_back()})))
    {
      executions->clear();
      return jewels::failure;
    }
  }
  return jewels::success;
}

template <ReplayChannel... InputChannels, ReplayChannel... OutputChannels>
jewels::BinaryOutcome
ExecutionQueue<jewels::meta::Types<InputChannels...>, jewels::meta::Types<OutputChannels...>>::finalize(
  jewels::Out<clockwork::Tappy<ExecutionStats>> stats)
{
  while (!empty())
  {
    if (jewels::fails(discard_next_execution()))
    {
      return jewels::failure;
    }
    if (stats_.get_completed() == 0U)
    {
      ++stats_.get_mutable_dropped_at_start();
    }
    else
    {
      ++stats_.get_mutable_dropped_at_end();
    }
  }
  *stats = stats_;
  return jewels::success;
}

template <ReplayChannel... InputChannels, ReplayChannel... OutputChannels>
bool ExecutionQueue<jewels::meta::Types<InputChannels...>, jewels::meta::Types<OutputChannels...>>::empty()
  const noexcept
{
  return next_execution_index_ == metadata_.executions.size();
}

template <ReplayChannel... InputChannels, ReplayChannel... OutputChannels>
const clockwork::Tappy<ExecutionStats>&
ExecutionQueue<jewels::meta::Types<InputChannels...>, jewels::meta::Types<OutputChannels...>>::stats() const noexcept
{
  return stats_;
}

template <ReplayChannel... InputChannels, ReplayChannel... OutputChannels>
size_t
ExecutionQueue<jewels::meta::Types<InputChannels...>, jewels::meta::Types<OutputChannels...>>::buffered_message_count()
  const noexcept
{
  return (std::get<ChannelBuffer<InputChannels>>(buffers_).buffer.size() + ... + 0U);
}

template <ReplayChannel... InputChannels, ReplayChannel... OutputChannels>
void ExecutionQueue<jewels::meta::Types<InputChannels...>, jewels::meta::Types<OutputChannels...>>::
  refresh_current_execution_state()
{
  if (empty())
  {
    (std::get<CurrentInputState<InputChannels>>(current_input_states_).missing_messages.clear(), ...);
    return;
  }
  const auto& execution = metadata_.executions[next_execution_index_];
  (refresh_missing_messages<InputChannels>(
     jewels::Out{std::get<CurrentInputState<InputChannels>>(current_input_states_).missing_messages},
     execution,
     std::get<ChannelBuffer<InputChannels>>(buffers_).buffer),
   ...);
}

template <ReplayChannel... InputChannels, ReplayChannel... OutputChannels>
bool ExecutionQueue<jewels::meta::Types<InputChannels...>, jewels::meta::Types<OutputChannels...>>::
  next_execution_is_ready() const
{
  return !empty() &&
         (std::get<CurrentInputState<InputChannels>>(current_input_states_).missing_messages.empty() && ...);
}

template <ReplayChannel... InputChannels, ReplayChannel... OutputChannels>
template <ReplayChannel Channel>
bool ExecutionQueue<jewels::meta::Types<InputChannels...>, jewels::meta::Types<OutputChannels...>>::
  pushed_message_skips_next_execution(const MessageKey& pushed_key) const
{
  if (empty())
  {
    return false;
  }
  const auto& missing_messages = std::get<CurrentInputState<Channel>>(current_input_states_).missing_messages;
  return !missing_messages.empty() && *missing_messages.begin() < pushed_key;
}

template <ReplayChannel... InputChannels, ReplayChannel... OutputChannels>
jewels::BinaryOutcome
ExecutionQueue<jewels::meta::Types<InputChannels...>, jewels::meta::Types<OutputChannels...>>::discard_next_execution()
{
  if (empty())
  {
    return jewels::failure;
  }
  if (jewels::fails(
        consume_execution_references<InputChannels...>(
          jewels::InOut{std::get<ChannelBuffer<InputChannels>>(buffers_).buffer}...,
          metadata_.executions[next_execution_index_])))
  {
    return jewels::failure;
  }
  ++next_execution_index_;
  refresh_current_execution_state();
  return jewels::success;
}

template <ReplayChannel... InputChannels, ReplayChannel... OutputChannels>
jewels::BinaryOutcome ExecutionQueue<jewels::meta::Types<InputChannels...>, jewels::meta::Types<OutputChannels...>>::
  assemble_and_consume_next_execution(jewels::Out<Execution> execution)
{
  if (empty())
  {
    return jewels::failure;
  }
  const auto& metadata = metadata_.executions[next_execution_index_];
  execution->journal_execution = metadata.journal_execution;
  if (!(jewels::ok(
          copy_inputs<InputChannels>(
            jewels::Out{std::get<ReplayInput<InputChannels>>(execution->inputs)},
            metadata,
            std::get<ChannelBuffer<InputChannels>>(buffers_).buffer)) &&
        ...))
  {
    return jewels::failure;
  }
  (copy_outputs<OutputChannels>(jewels::Out{std::get<ReplayOutput<OutputChannels>>(execution->outputs)}, metadata),
   ...);
  if (jewels::fails(
        consume_execution_references<InputChannels...>(
          jewels::InOut{std::get<ChannelBuffer<InputChannels>>(buffers_).buffer}..., metadata)))
  {
    return jewels::failure;
  }
  ++next_execution_index_;
  ++stats_.get_mutable_completed();
  refresh_current_execution_state();
  return jewels::success;
}

template <ReplayChannel... InputChannels, ReplayChannel... OutputChannels>
void ExecutionQueue<jewels::meta::Types<InputChannels...>, jewels::meta::Types<OutputChannels...>>::record_stream_drop()
{
  if (stats_.get_completed() == 0U)
  {
    ++stats_.get_mutable_dropped_at_start();
  }
  else
  {
    ++stats_.get_mutable_dropped_in_middle();
  }
}

} // namespace clockwork::journal::replay
