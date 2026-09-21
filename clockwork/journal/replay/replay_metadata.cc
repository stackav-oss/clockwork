// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/journal/replay/replay_metadata.hh"

#include "clockwork/journal/journal.pb.h"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/memory/pointers.hh"

#include <boost/container_hash/hash.hpp>
#include <google/protobuf/repeated_ptr_field.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>

namespace clockwork::journal::replay
{
namespace
{

using ChannelSequenceKey = std::pair<std::string, uint64_t>;

using MessageIndexValue = jewels::memory::NonNullPtr<const ChannelMessage*>;
using MessageIndex = std::unordered_map<ChannelSequenceKey, MessageIndexValue, boost::hash<ChannelSequenceKey>>;
using MessageCounts = std::unordered_map<MessageKey, size_t, boost::hash<MessageKey>>;
using RequirementCounts = std::unordered_map<std::string, MessageCounts>;

jewels::BinaryOutcome build_message_index(jewels::Out<MessageIndex> message_index, const JournalFile& journal_file)
{
  message_index->clear();
  for (const auto& message : journal_file.channel_messages())
  {
    const auto key = ChannelSequenceKey{message.channel_name(), message.sequence_number()};
    if (!message_index->try_emplace(key, jewels::memory::make_non_null_from_ref(message)).second)
    {
      return jewels::failure;
    }
  }
  return jewels::success;
}

jewels::BinaryOutcome append_execution_inputs(
  jewels::Out<std::vector<ExecutionInput>> inputs,
  jewels::InOut<RequirementCounts> requirement_counts,
  const CogExecution& execution)
{
  inputs->clear();
  inputs->reserve(static_cast<size_t>(execution.input_views_size()));
  for (const auto& input_view : execution.input_views())
  {
    const auto& sequence_numbers = input_view.visible_sequence_numbers();
    if (
      input_view.first_new_index() > static_cast<uint32_t>(sequence_numbers.size()) ||
      std::ranges::adjacent_find(sequence_numbers, std::greater_equal{}) != sequence_numbers.end())
    {
      return jewels::failure;
    }
    auto& execution_input = inputs->emplace_back(
      ExecutionInput{
        .journal_state = input_view,
        .messages = {},
      });
    execution_input.messages.reserve(static_cast<size_t>(sequence_numbers.size()));
    for (const auto sequence_number : sequence_numbers)
    {
      execution_input.messages.push_back(sequence_number);
      ++(*requirement_counts)[input_view.channel_name()][sequence_number];
    }
  }
  return jewels::success;
}

jewels::BinaryOutcome append_execution_outputs(
  jewels::Out<std::vector<ExecutionOutput>> outputs, const CogExecution& execution, const MessageIndex& message_index)
{
  outputs->clear();
  outputs->reserve(static_cast<size_t>(execution.outputs_size()));
  for (const auto& output : execution.outputs())
  {
    auto& execution_output = outputs->emplace_back(
      ExecutionOutput{
        .journal_state = output,
        .messages = {},
      });
    execution_output.messages.reserve(static_cast<size_t>(output.produced_sequence_numbers_size()));
    for (const auto sequence_number : output.produced_sequence_numbers())
    {
      const auto message_iter = message_index.find(ChannelSequenceKey{output.channel_name(), sequence_number});
      if (message_iter == message_index.end())
      {
        return jewels::failure;
      }
      execution_output.messages.push_back(*message_iter->second);
    }
  }
  return jewels::success;
}

} // namespace

jewels::BinaryOutcome make_replay_metadata(
  jewels::Out<ReplayMetadata> metadata, const JournalFile& journal_file, const size_t cog_journal_index)
{
  metadata->input_channels.clear();
  metadata->executions.clear();
  if (cog_journal_index >= static_cast<size_t>(journal_file.cog_journals_size()))
  {
    return jewels::failure;
  }

  MessageIndex message_index;
  if (jewels::fails(build_message_index(jewels::Out{message_index}, journal_file)))
  {
    return jewels::failure;
  }

  RequirementCounts requirement_counts;
  const auto& cog_journal = journal_file.cog_journals(static_cast<int>(cog_journal_index));
  metadata->executions.reserve(static_cast<size_t>(cog_journal.executions_size()));
  for (const auto& execution : cog_journal.executions())
  {
    auto& execution_metadata = metadata->executions.emplace_back(
      ExecutionMetadata{
        .journal_execution = execution,
        .inputs = {},
        .outputs = {},
      });
    if (jewels::fails(append_execution_inputs(
          jewels::Out{execution_metadata.inputs}, jewels::InOut{requirement_counts}, execution)))
    {
      return jewels::failure;
    }
    if (jewels::fails(append_execution_outputs(jewels::Out{execution_metadata.outputs}, execution, message_index)))
    {
      return jewels::failure;
    }
  }

  metadata->input_channels.reserve(requirement_counts.size());
  for (auto& [channel_name, channel_counts] : requirement_counts)
  {
    auto& channel_requirements = metadata->input_channels.emplace_back(
      ChannelRequirements{
        .channel_name = channel_name,
        .messages = {},
      });
    channel_requirements.messages.reserve(channel_counts.size());
    for (const auto& [key, reference_count] : channel_counts)
    {
      channel_requirements.messages.push_back(
        MessageRequirement{
          .key = key,
          .reference_count = reference_count,
        });
    }
    std::ranges::sort(channel_requirements.messages, {}, &MessageRequirement::key);
  }
  return jewels::success;
}

} // namespace clockwork::journal::replay
