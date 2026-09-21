// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/journal/journal.pb.h"
#include "clockwork/journal/replay/replay_metadata.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"

#include <catch2/catch_test_macros.hpp>
#include <google/protobuf/repeated_ptr_field.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace clockwork::journal::replay
{
namespace
{

ChannelMessage* add_channel_message(
  JournalFile& journal_file,
  const std::string& channel_name,
  const uint64_t sequence_number,
  const int64_t publish_time_ns)
{
  auto* message = journal_file.add_channel_messages();
  message->set_channel_name(channel_name);
  message->set_sequence_number(sequence_number);
  message->set_publish_time_ns(publish_time_ns);
  return message;
}

TEST_CASE("make_replay_metadata preserves execution order and groups sorted input requirements")
{
  JournalFile journal_file;
  auto* journal = journal_file.add_cog_journals();
  auto* first_execution = journal->add_executions();
  first_execution->set_execution_index(4U);
  auto* first_input = first_execution->add_input_views();
  first_input->set_channel_name("input");
  first_input->add_visible_sequence_numbers(10U);
  auto* first_output = first_execution->add_outputs();
  first_output->set_channel_name("output");
  first_output->add_produced_sequence_numbers(21U);
  first_output->add_produced_sequence_numbers(20U);

  auto* second_execution = journal->add_executions();
  second_execution->set_execution_index(5U);
  auto* second_input = second_execution->add_input_views();
  second_input->set_channel_name("input");
  second_input->add_visible_sequence_numbers(10U);
  second_input->add_visible_sequence_numbers(11U);
  auto* other_input = second_execution->add_input_views();
  other_input->set_channel_name("other_input");
  other_input->add_visible_sequence_numbers(3U);

  add_channel_message(journal_file, "input", 10U, 1000);
  add_channel_message(journal_file, "input", 11U, 1100);
  add_channel_message(journal_file, "other_input", 3U, 300);
  add_channel_message(journal_file, "output", 20U, 2000);
  add_channel_message(journal_file, "output", 21U, 2100);

  ReplayMetadata metadata;
  REQUIRE(jewels::ok(make_replay_metadata(jewels::Out{metadata}, journal_file, 0U)));

  REQUIRE(metadata.input_channels.size() == 2U);
  const auto input_channel = std::ranges::find(metadata.input_channels, "input", &ChannelRequirements::channel_name);
  REQUIRE(input_channel != metadata.input_channels.end());
  REQUIRE(input_channel->messages.size() == 2U);
  CHECK(input_channel->messages[0].key == 10U);
  CHECK(input_channel->messages[0].reference_count == 2U);
  CHECK(input_channel->messages[1].key == 11U);
  CHECK(input_channel->messages[1].reference_count == 1U);

  const auto other_channel =
    std::ranges::find(metadata.input_channels, "other_input", &ChannelRequirements::channel_name);
  REQUIRE(other_channel != metadata.input_channels.end());
  REQUIRE(other_channel->messages.size() == 1U);
  CHECK(other_channel->messages[0].key == 3U);
  CHECK(other_channel->messages[0].reference_count == 1U);

  REQUIRE(metadata.executions.size() == 2U);
  CHECK(metadata.executions[0].journal_execution.execution_index() == 4U);
  CHECK(metadata.executions[1].journal_execution.execution_index() == 5U);
  REQUIRE(metadata.executions[0].inputs.size() == 1U);
  CHECK(metadata.executions[0].inputs[0].messages[0] == 10U);
  REQUIRE(metadata.executions[1].inputs.size() == 2U);
  REQUIRE(metadata.executions[1].inputs[0].messages.size() == 2U);
  CHECK(metadata.executions[1].inputs[0].messages[0] == 10U);
  CHECK(metadata.executions[1].inputs[0].messages[1] == 11U);
  REQUIRE(metadata.executions[1].inputs[1].messages.size() == 1U);
  CHECK(metadata.executions[1].inputs[1].messages[0] == 3U);
  REQUIRE(metadata.executions[0].outputs.size() == 1U);
  REQUIRE(metadata.executions[0].outputs[0].messages.size() == 2U);
  CHECK(metadata.executions[0].outputs[0].messages[0].sequence_number() == 21U);
  CHECK(metadata.executions[0].outputs[0].messages[1].sequence_number() == 20U);
}

TEST_CASE("make_replay_metadata rejects invalid input view sequence metadata")
{
  JournalFile journal_file;
  auto* input = journal_file.add_cog_journals()->add_executions()->add_input_views();
  input->set_channel_name("input");
  add_channel_message(journal_file, "input", 1U, 100);
  add_channel_message(journal_file, "input", 2U, 200);

  ReplayMetadata metadata;

  SECTION("sequence numbers are decreasing")
  {
    input->add_visible_sequence_numbers(2U);
    input->add_visible_sequence_numbers(1U);
    CHECK(jewels::fails(make_replay_metadata(jewels::Out{metadata}, journal_file, 0U)));
  }

  SECTION("sequence numbers are duplicated")
  {
    input->add_visible_sequence_numbers(1U);
    input->add_visible_sequence_numbers(1U);
    CHECK(jewels::fails(make_replay_metadata(jewels::Out{metadata}, journal_file, 0U)));
  }

  SECTION("first new index is past the input view")
  {
    input->add_visible_sequence_numbers(1U);
    input->set_first_new_index(2U);
    CHECK(jewels::fails(make_replay_metadata(jewels::Out{metadata}, journal_file, 0U)));
  }

  SECTION("first new index may equal the input view size")
  {
    input->add_visible_sequence_numbers(1U);
    input->set_first_new_index(1U);
    CHECK(jewels::ok(make_replay_metadata(jewels::Out{metadata}, journal_file, 0U)));
  }
}

TEST_CASE("make_replay_metadata permits missing input message metadata")
{
  JournalFile journal_file;
  auto* input = journal_file.add_cog_journals()->add_executions()->add_input_views();
  input->set_channel_name("input");
  input->add_visible_sequence_numbers(1U);

  REQUIRE(journal_file.channel_messages().empty());
  ReplayMetadata metadata;
  REQUIRE(jewels::ok(make_replay_metadata(jewels::Out{metadata}, journal_file, 0U)));
  REQUIRE(metadata.input_channels.size() == 1U);
  REQUIRE(metadata.input_channels.front().messages.size() == 1U);
  CHECK(metadata.input_channels.front().messages.front().key == 1U);
  CHECK(metadata.input_channels.front().messages.front().reference_count == 1U);
}

TEST_CASE("make_replay_metadata requires metadata for every referenced output message")
{
  JournalFile journal_file;
  auto* output = journal_file.add_cog_journals()->add_executions()->add_outputs();
  output->set_channel_name("output");
  output->add_produced_sequence_numbers(1U);

  ReplayMetadata metadata;
  CHECK(jewels::fails(make_replay_metadata(jewels::Out{metadata}, journal_file, 0U)));
}

TEST_CASE("make_replay_metadata rejects duplicate channel message metadata")
{
  JournalFile journal_file;
  journal_file.add_cog_journals();
  add_channel_message(journal_file, "channel", 1U, 100);
  add_channel_message(journal_file, "channel", 1U, 200);

  ReplayMetadata metadata;
  CHECK(jewels::fails(make_replay_metadata(jewels::Out{metadata}, journal_file, 0U)));
}

TEST_CASE("make_replay_metadata accepts an empty cog and validates its index")
{
  JournalFile journal_file;
  journal_file.add_cog_journals()->add_executions()->set_execution_index(4U);
  journal_file.add_cog_journals();

  ReplayMetadata metadata;
  REQUIRE(jewels::ok(make_replay_metadata(jewels::Out{metadata}, journal_file, 1U)));
  CHECK(metadata.input_channels.empty());
  CHECK(metadata.executions.empty());
  CHECK(jewels::fails(make_replay_metadata(jewels::Out{metadata}, journal_file, 2U)));
}

} // namespace
} // namespace clockwork::journal::replay
