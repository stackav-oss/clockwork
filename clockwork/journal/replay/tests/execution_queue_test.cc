// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/journal/journal.pb.h"
#include "clockwork/journal/replay/execution_queue.hh"
#include "clockwork/journal/replay/execution_stats_clk_cc.hh"
#include "clockwork/journal/replay/message_buffer.hh"
#include "clockwork/journal/replay/replay_metadata.hh"
#include "clockwork/journal/replay/tests/support/execution_queue_test_messages_clk_cc.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/readers/types.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/meta/types.hh"

#include <catch2/catch_test_macros.hpp>
#include <google/protobuf/repeated_ptr_field.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace clockwork::journal::replay
{
namespace
{

using TestMessage = clockwork::Tappy<tests::IntegralTestMessage>;
using OtherMessage = clockwork::Tappy<tests::StringTestMessage>;

struct ChannelA
{
  using MsgType = TestMessage;
  static constexpr std::string_view name = "channel_a";
};

struct ChannelB
{
  using MsgType = TestMessage;
  static constexpr std::string_view name = "channel_b";
};

struct DuplicateChannelA
{
  using MsgType = TestMessage;
  static constexpr std::string_view name = "channel_a";
};

struct QueueChannelB
{
  using MsgType = OtherMessage;
  static constexpr std::string_view name = "channel_b";
};

struct Output
{
  using MsgType = TestMessage;
  static constexpr std::string_view name = "output";
};

using Queue = ExecutionQueue<jewels::meta::Types<ChannelA, QueueChannelB>, jewels::meta::Types<Output>>;
using Execution = Queue::Execution;
using InputQueue = ExecutionQueue<jewels::meta::Types<ChannelA>, jewels::meta::Types<Output>>;

Tappy<ExecutionStats> make_stats(
  const uint64_t dropped_at_start,
  const uint64_t dropped_in_middle,
  const uint64_t dropped_at_end,
  const uint64_t completed,
  const uint64_t total)
{
  Tappy<ExecutionStats> stats;
  stats.set_dropped_at_start(dropped_at_start);
  stats.set_dropped_in_middle(dropped_in_middle);
  stats.set_dropped_at_end(dropped_at_end);
  stats.set_completed(completed);
  stats.set_total(total);
  return stats;
}

struct ExpectedExecution
{
  struct ChannelAMessage
  {
    uint64_t sequence_number;
    uint64_t value;
  };

  struct ChannelBMessage
  {
    uint64_t sequence_number;
    std::string_view value;
  };

  uint64_t execution_index;
  std::vector<ChannelAMessage> channel_a_messages;
  std::vector<ChannelBMessage> channel_b_messages;
  std::vector<uint64_t> output_sequences;
};

void add_header(JournalFile& journal_file, const std::string_view channel, const uint64_t sequence)
{
  for (const auto& message : journal_file.channel_messages())
  {
    if (message.channel_name() == channel && message.sequence_number() == sequence)
    {
      return;
    }
  }
  auto* message = journal_file.add_channel_messages();
  message->set_channel_name(channel);
  message->set_sequence_number(sequence);
  message->set_publish_time_ns(static_cast<int64_t>(sequence * 10U));
}

template <std::integral... SequenceNumbers>
void add_input(
  JournalFile& journal_file,
  CogExecution& execution,
  const std::string_view channel,
  const SequenceNumbers... sequences)
{
  auto* input = execution.add_input_views();
  input->set_channel_name(channel);
  (input->add_visible_sequence_numbers(static_cast<uint64_t>(sequences)), ...);
  (add_header(journal_file, channel, static_cast<uint64_t>(sequences)), ...);
}

template <std::integral... SequenceNumbers>
void add_output(
  JournalFile& journal_file,
  CogExecution& execution,
  const std::string_view channel,
  const SequenceNumbers... sequences)
{
  auto* output = execution.add_outputs();
  output->set_channel_name(channel);
  (output->add_produced_sequence_numbers(static_cast<uint64_t>(sequences)), ...);
  (add_header(journal_file, channel, static_cast<uint64_t>(sequences)), ...);
}

clockwork_logging::LoggedMessage make_logged_message(const std::string_view channel, const uint32_t sequence)
{
  auto message = clockwork_logging::LoggedMessage{};
  message.topic = channel;
  message.sequence_number = sequence;
  message.publish_time = clockwork_logging::LogTimestamp{static_cast<int64_t>(sequence * 10U)};
  return message;
}

TestMessage make_test_message(const uint64_t value)
{
  auto message = TestMessage{};
  message.set_value(value);
  return message;
}

OtherMessage make_other_message(const std::string_view value)
{
  auto message = OtherMessage{};
  message.get_underlying_value().set_truncate(value);
  return message;
}

void check_execution(const Execution& execution, const ExpectedExecution& expected)
{
  CHECK(execution.journal_execution.execution_index() == expected.execution_index);

  const auto& channel_a_messages = std::get<ReplayInput<ChannelA>>(execution.inputs).messages;
  REQUIRE(channel_a_messages.size() == expected.channel_a_messages.size());
  for (size_t index = 0U; index < channel_a_messages.size(); ++index)
  {
    CHECK(channel_a_messages[index]->sequence_number == expected.channel_a_messages[index].sequence_number);
    CHECK(channel_a_messages[index]->message.get_value() == expected.channel_a_messages[index].value);
  }

  const auto& channel_b_messages = std::get<ReplayInput<QueueChannelB>>(execution.inputs).messages;
  REQUIRE(channel_b_messages.size() == expected.channel_b_messages.size());
  for (size_t index = 0U; index < channel_b_messages.size(); ++index)
  {
    CHECK(channel_b_messages[index]->sequence_number == expected.channel_b_messages[index].sequence_number);
    CHECK(channel_b_messages[index]->message.get_value() == expected.channel_b_messages[index].value);
  }

  const auto& output_messages = std::get<ReplayOutput<Output>>(execution.outputs).messages;
  REQUIRE(output_messages.size() == expected.output_sequences.size());
  for (size_t index = 0U; index < output_messages.size(); ++index)
  {
    CHECK(output_messages[index].sequence_number() == expected.output_sequences[index]);
  }
}

TEST_CASE("has_unique_channel_name requires exactly one matching channel")
{
  CHECK(has_unique_channel_name<ChannelA, ChannelB>("channel_a"));
  CHECK_FALSE(has_unique_channel_name<ChannelA, ChannelB>("unknown"));
  CHECK_FALSE(has_unique_channel_name<ChannelA, DuplicateChannelA>("channel_a"));
  CHECK_FALSE(has_unique_channel_name<>("channel_a"));
}

TEST_CASE("metadata_channels_match_configuration validates channel types and uniqueness")
{
  auto metadata = ReplayMetadata{};
  auto& execution = metadata.executions.emplace_back();
  execution.inputs.emplace_back().journal_state.set_channel_name("channel_a");
  execution.outputs.emplace_back().journal_state.set_channel_name("channel_b");

  using InputChannels = jewels::meta::Types<ChannelA>;
  using OutputChannels = jewels::meta::Types<ChannelB>;
  CHECK(metadata_channels_match_configuration(InputChannels{}, OutputChannels{}, metadata));

  SECTION("unknown input channel")
  {
    execution.inputs.front().journal_state.set_channel_name("unknown");
    CHECK_FALSE(metadata_channels_match_configuration(InputChannels{}, OutputChannels{}, metadata));
  }

  SECTION("unknown output channel")
  {
    execution.outputs.front().journal_state.set_channel_name("unknown");
    CHECK_FALSE(metadata_channels_match_configuration(InputChannels{}, OutputChannels{}, metadata));
  }

  SECTION("duplicate input metadata")
  {
    execution.inputs.push_back(execution.inputs.front());
    CHECK_FALSE(metadata_channels_match_configuration(InputChannels{}, OutputChannels{}, metadata));
  }

  SECTION("duplicate output metadata")
  {
    execution.outputs.push_back(execution.outputs.front());
    CHECK_FALSE(metadata_channels_match_configuration(InputChannels{}, OutputChannels{}, metadata));
  }

  SECTION("duplicate configured channel name")
  {
    using DuplicateInputChannels = jewels::meta::Types<ChannelA, DuplicateChannelA>;
    CHECK_FALSE(metadata_channels_match_configuration(DuplicateInputChannels{}, OutputChannels{}, metadata));
  }
}

TEST_CASE("copy_inputs reconstructs configured messages without consuming references")
{
  const auto requirements = std::vector{ChannelRequirements{
    .channel_name = "channel_a",
    .messages =
      {
        {.key = 1U, .reference_count = 1U},
        {.key = 2U, .reference_count = 1U},
      },
  }};
  auto buffer_result = jewels::FactoryResult<MessageBuffer<TestMessage>>{};
  REQUIRE(jewels::ok(MessageBuffer<TestMessage>::try_make(jewels::FactoryOut{buffer_result}, requirements)));
  REQUIRE(buffer_result->push(make_logged_message("channel_a", 1U), TestMessage{}));
  REQUIRE(buffer_result->push(make_logged_message("channel_a", 2U), TestMessage{}));

  auto execution = ExecutionMetadata{};
  execution.inputs.emplace_back(
    ExecutionInput{
      .journal_state = {},
      .messages = {1U, 2U},
    });
  execution.inputs.back().journal_state.set_channel_name("channel_a");
  auto input = ReplayInput<ChannelA>{};

  REQUIRE(jewels::ok(copy_inputs<ChannelA>(jewels::Out{input}, execution, *buffer_result)));
  CHECK(input.journal_state.channel_name() == "channel_a");
  REQUIRE(input.messages.size() == 2U);
  CHECK(input.messages[0]->sequence_number == 1U);
  CHECK(input.messages[1]->sequence_number == 2U);
  CHECK(buffer_result->size() == 2U);
  CHECK(jewels::ok(buffer_result->consume("channel_a", 1U)));
  CHECK(jewels::ok(buffer_result->consume("channel_a", 2U)));
}

TEST_CASE("copy_inputs returns an empty configured input when metadata is absent")
{
  auto buffer_result = jewels::FactoryResult<MessageBuffer<TestMessage>>{};
  REQUIRE(
    jewels::ok(
      MessageBuffer<TestMessage>::try_make(jewels::FactoryOut{buffer_result}, std::vector<ChannelRequirements>{})));
  auto execution = ExecutionMetadata{};
  auto input = ReplayInput<ChannelA>{};
  input.journal_state.set_channel_name("other");
  input.messages.emplace_back();

  REQUIRE(jewels::ok(copy_inputs<ChannelA>(jewels::Out{input}, execution, *buffer_result)));
  CHECK(input.journal_state.channel_name() == "channel_a");
  CHECK(input.messages.empty());
}

TEST_CASE("copy_inputs fails without consuming an unavailable message reference")
{
  const auto requirements = std::vector{ChannelRequirements{
    .channel_name = "channel_a",
    .messages = {{.key = 1U, .reference_count = 1U}},
  }};
  auto buffer_result = jewels::FactoryResult<MessageBuffer<TestMessage>>{};
  REQUIRE(jewels::ok(MessageBuffer<TestMessage>::try_make(jewels::FactoryOut{buffer_result}, requirements)));
  auto execution = ExecutionMetadata{};
  execution.inputs.emplace_back(
    ExecutionInput{
      .journal_state = {},
      .messages = {1U},
    });
  execution.inputs.back().journal_state.set_channel_name("channel_a");
  auto input = ReplayInput<ChannelA>{};

  CHECK(jewels::fails(copy_inputs<ChannelA>(jewels::Out{input}, execution, *buffer_result)));
  CHECK(jewels::ok(buffer_result->consume("channel_a", 1U)));
}

TEST_CASE("copy_outputs reconstructs matching output metadata")
{
  auto execution = ExecutionMetadata{};
  execution.outputs.emplace_back(
    ExecutionOutput{
      .journal_state = {},
      .messages = {ChannelMessage{}},
    });
  execution.outputs.back().journal_state.set_channel_name("channel_b");
  execution.outputs.back().messages.front().set_sequence_number(7U);
  auto output = ReplayOutput<ChannelB>{};

  copy_outputs<ChannelB>(jewels::Out{output}, execution);
  CHECK(output.journal_state.channel_name() == "channel_b");
  REQUIRE(output.messages.size() == 1U);
  CHECK(output.messages.front().sequence_number() == 7U);
}

TEST_CASE("copy_outputs returns empty configured output metadata when metadata is absent")
{
  auto execution = ExecutionMetadata{};
  auto output = ReplayOutput<ChannelB>{};
  output.journal_state.set_channel_name("other");
  output.messages.emplace_back();

  copy_outputs<ChannelB>(jewels::Out{output}, execution);
  CHECK(output.journal_state.channel_name() == "channel_b");
  CHECK(output.messages.empty());
}

TEST_CASE("refresh_missing_messages retains only unavailable requirements")
{
  const auto requirements = std::vector{ChannelRequirements{
    .channel_name = "channel_a",
    .messages =
      {
        {.key = 1U, .reference_count = 1U},
        {.key = 2U, .reference_count = 1U},
      },
  }};
  auto buffer_result = jewels::FactoryResult<MessageBuffer<TestMessage>>{};
  REQUIRE(jewels::ok(MessageBuffer<TestMessage>::try_make(jewels::FactoryOut{buffer_result}, requirements)));
  const auto logged_message = make_logged_message("channel_a", 1U);
  REQUIRE(buffer_result->push(logged_message, TestMessage{}));

  auto execution = ExecutionMetadata{};
  execution.inputs.emplace_back(
    ExecutionInput{
      .journal_state = {},
      .messages = {1U, 2U},
    });
  execution.inputs.back().journal_state.set_channel_name("channel_a");
  auto missing_messages = std::set<MessageKey>{99U};
  refresh_missing_messages<ChannelA>(jewels::Out{missing_messages}, execution, *buffer_result);
  CHECK(missing_messages == std::set<MessageKey>{2U});

  execution.inputs.clear();
  refresh_missing_messages<ChannelA>(jewels::Out{missing_messages}, execution, *buffer_result);
  CHECK(missing_messages.empty());
}

TEST_CASE("consume_execution_references consumes matching references across typed channel buffers")
{
  const auto requirements_a = std::vector{ChannelRequirements{
    .channel_name = "channel_a",
    .messages =
      {
        {.key = 1U, .reference_count = 1U},
        {.key = 2U, .reference_count = 2U},
      },
  }};
  const auto requirements_b = std::vector{ChannelRequirements{
    .channel_name = "channel_b",
    .messages = {{.key = 3U, .reference_count = 1U}},
  }};
  auto buffer_a = jewels::FactoryResult<MessageBuffer<TestMessage>>{};
  auto buffer_b = jewels::FactoryResult<MessageBuffer<TestMessage>>{};
  REQUIRE(jewels::ok(MessageBuffer<TestMessage>::try_make(jewels::FactoryOut{buffer_a}, requirements_a)));
  REQUIRE(jewels::ok(MessageBuffer<TestMessage>::try_make(jewels::FactoryOut{buffer_b}, requirements_b)));

  auto execution = ExecutionMetadata{};
  execution.inputs.emplace_back(
    ExecutionInput{
      .journal_state = {},
      .messages = {1U, 2U},
    });
  execution.inputs.back().journal_state.set_channel_name("channel_a");
  execution.inputs.emplace_back(
    ExecutionInput{
      .journal_state = {},
      .messages = {3U},
    });
  execution.inputs.back().journal_state.set_channel_name("channel_b");

  REQUIRE(
    jewels::ok(
      consume_execution_references<ChannelA, ChannelB>(jewels::InOut{*buffer_a}, jewels::InOut{*buffer_b}, execution)));
  CHECK(jewels::fails(buffer_a->consume("channel_a", 1U)));
  CHECK(jewels::ok(buffer_a->consume("channel_a", 2U)));
  CHECK(jewels::fails(buffer_a->consume("channel_a", 2U)));
  CHECK(jewels::fails(buffer_b->consume("channel_b", 3U)));
}

TEST_CASE("consume_execution_references handles empty and invalid execution inputs")
{
  const auto requirements = std::vector{ChannelRequirements{
    .channel_name = "channel_a",
    .messages = {{.key = 1U, .reference_count = 1U}},
  }};
  auto buffer = jewels::FactoryResult<MessageBuffer<TestMessage>>{};
  REQUIRE(jewels::ok(MessageBuffer<TestMessage>::try_make(jewels::FactoryOut{buffer}, requirements)));
  auto execution = ExecutionMetadata{};

  SECTION("no inputs")
  {
    CHECK(jewels::ok(consume_execution_references<ChannelA>(jewels::InOut{*buffer}, execution)));
  }

  SECTION("unknown channel")
  {
    execution.inputs.emplace_back(
      ExecutionInput{
        .journal_state = {},
        .messages = {1U},
      });
    execution.inputs.back().journal_state.set_channel_name("unknown");
    CHECK(jewels::fails(consume_execution_references<ChannelA>(jewels::InOut{*buffer}, execution)));
  }

  SECTION("unrequired reference")
  {
    execution.inputs.emplace_back(
      ExecutionInput{
        .journal_state = {},
        .messages = {2U},
      });
    execution.inputs.back().journal_state.set_channel_name("channel_a");
    CHECK(jewels::fails(consume_execution_references<ChannelA>(jewels::InOut{*buffer}, execution)));
  }

  CHECK(jewels::ok(buffer->consume("channel_a", 1U)));
}

TEST_CASE("ExecutionQueue accepts only non-empty unique channel type packs")
{
  STATIC_REQUIRE(ReplayChannelTypePack<ChannelA>);
  STATIC_REQUIRE(ReplayChannelTypePack<ChannelA, QueueChannelB>);
  STATIC_REQUIRE_FALSE(ReplayChannelTypePack<>);
  STATIC_REQUIRE_FALSE(ReplayChannelTypePack<ChannelA, ChannelA>);
}

TEST_CASE("ExecutionQueue preserves shared typed messages across completed and dropped executions")
{
  JournalFile journal_file;
  auto* journal = journal_file.add_cog_journals();
  auto* first_execution = journal->add_executions();
  first_execution->set_execution_index(1U);
  add_input(journal_file, *first_execution, "channel_a", 4U, 5U);
  add_input(journal_file, *first_execution, "channel_b", 1U);
  add_output(journal_file, *first_execution, "output", 11U);

  auto* second_execution = journal->add_executions();
  second_execution->set_execution_index(2U);
  add_input(journal_file, *second_execution, "channel_a", 5U);
  add_input(journal_file, *second_execution, "channel_b", 1U);
  add_output(journal_file, *second_execution, "output", 12U);

  auto* third_execution = journal->add_executions();
  third_execution->set_execution_index(3U);
  add_input(journal_file, *third_execution, "channel_a", 6U);
  add_input(journal_file, *third_execution, "channel_b", 3U);
  add_output(journal_file, *third_execution, "output", 13U);

  jewels::FactoryResult<Queue> queue_result;
  REQUIRE(jewels::ok(Queue::try_make(jewels::Out{queue_result}, journal_file, 0U)));
  std::vector<Execution> executions;
  auto expected_stats = make_stats(0U, 0U, 0U, 0U, 3U);

  auto message = make_logged_message("channel_b", 1U);
  REQUIRE(jewels::ok(queue_result->push<QueueChannelB>(jewels::Out{executions}, message, make_other_message("b1"))));
  CHECK(executions.empty());
  CHECK(queue_result->buffered_message_count() == 1U);

  SECTION("one push completes both executions")
  {
    message = make_logged_message("channel_a", 4U);
    REQUIRE(jewels::ok(queue_result->push<ChannelA>(jewels::Out{executions}, message, make_test_message(104U))));
    CHECK(executions.empty());
    CHECK(queue_result->buffered_message_count() == 2U);

    message = make_logged_message("channel_a", 5U);
    REQUIRE(jewels::ok(queue_result->push<ChannelA>(jewels::Out{executions}, message, make_test_message(105U))));
    REQUIRE(executions.size() == 2U);
    check_execution(
      executions[0],
      ExpectedExecution{
        .execution_index = 1U,
        .channel_a_messages =
          {
            {.sequence_number = 4U, .value = 104U},
            {.sequence_number = 5U, .value = 105U},
          },
        .channel_b_messages =
          {
            {.sequence_number = 1U, .value = "b1"},
          },
        .output_sequences = {11U},
      });
    check_execution(
      executions[1],
      ExpectedExecution{
        .execution_index = 2U,
        .channel_a_messages =
          {
            {.sequence_number = 5U, .value = 105U},
          },
        .channel_b_messages =
          {
            {.sequence_number = 1U, .value = "b1"},
          },
        .output_sequences = {12U},
      });
    const auto& first_a_messages = std::get<ReplayInput<ChannelA>>(executions[0].inputs).messages;
    const auto& second_a_messages = std::get<ReplayInput<ChannelA>>(executions[1].inputs).messages;
    CHECK(first_a_messages[1].get() == second_a_messages[0].get());
    const auto& first_b_messages = std::get<ReplayInput<QueueChannelB>>(executions[0].inputs).messages;
    const auto& second_b_messages = std::get<ReplayInput<QueueChannelB>>(executions[1].inputs).messages;
    CHECK(first_b_messages[0].get() == second_b_messages[0].get());
    expected_stats.set_completed(2U);
  }

  SECTION("shared messages survive a dropped execution")
  {
    message = make_logged_message("channel_a", 5U);
    REQUIRE(jewels::ok(queue_result->push<ChannelA>(jewels::Out{executions}, message, make_test_message(105U))));
    REQUIRE(executions.size() == 1U);
    check_execution(
      executions[0],
      ExpectedExecution{
        .execution_index = 2U,
        .channel_a_messages =
          {
            {.sequence_number = 5U, .value = 105U},
          },
        .channel_b_messages =
          {
            {.sequence_number = 1U, .value = "b1"},
          },
        .output_sequences = {12U},
      });
    expected_stats.set_dropped_at_start(1U);
    expected_stats.set_completed(1U);
  }

  CHECK(queue_result->buffered_message_count() == 0U);
  // Push past the final execution's missing input to discard it as incomplete and record a middle drop.
  message = make_logged_message("channel_a", 7U);
  REQUIRE(jewels::ok(queue_result->push<ChannelA>(jewels::Out{executions}, message, make_test_message(107U))));
  CHECK(executions.empty());
  CHECK(queue_result->empty());
  CHECK(queue_result->buffered_message_count() == 0U);
  expected_stats.set_dropped_in_middle(1U);
  CHECK(queue_result->stats() == expected_stats);
}

TEST_CASE("ExecutionQueue ignores invalid messages without dropping valid work")
{
  JournalFile journal_file;
  auto* execution = journal_file.add_cog_journals()->add_executions();
  add_input(journal_file, *execution, "channel_a", 2U);
  jewels::FactoryResult<InputQueue> queue_result;
  REQUIRE(jewels::ok(InputQueue::try_make(jewels::Out{queue_result}, journal_file, 0U)));
  std::vector<InputQueue::Execution> executions;

  SECTION("mismatched callback topic")
  {
    executions.emplace_back();
    const auto message = make_logged_message("channel_b", 2U);
    CHECK(jewels::fails(queue_result->push<ChannelA>(jewels::Out{executions}, message, TestMessage{})));
    CHECK(executions.empty());
  }

  SECTION("stale message")
  {
    const auto message = make_logged_message("channel_a", 1U);
    REQUIRE(jewels::ok(queue_result->push<ChannelA>(jewels::Out{executions}, message, TestMessage{})));
    CHECK(executions.empty());
  }

  CHECK_FALSE(queue_result->empty());
  CHECK(queue_result->stats().get_completed() == 0U);
  CHECK(queue_result->stats().get_dropped_at_start() == 0U);
  const auto message = make_logged_message("channel_a", 2U);
  REQUIRE(jewels::ok(queue_result->push<ChannelA>(jewels::Out{executions}, message, TestMessage{})));
  CHECK(executions.size() == 1U);
  CHECK(queue_result->empty());
  CHECK(queue_result->stats().get_completed() == 1U);
}

TEST_CASE("ExecutionQueue drops an initial execution with missing input message metadata")
{
  JournalFile journal_file;
  auto* journal = journal_file.add_cog_journals();
  auto* first_execution = journal->add_executions();
  first_execution->set_execution_index(1U);
  auto* missing_input = first_execution->add_input_views();
  missing_input->set_channel_name("channel_a");
  missing_input->add_visible_sequence_numbers(1U);
  auto* second_execution = journal->add_executions();
  second_execution->set_execution_index(2U);
  add_input(journal_file, *second_execution, "channel_a", 2U);

  REQUIRE(journal_file.channel_messages_size() == 1);
  CHECK(journal_file.channel_messages(0).channel_name() == "channel_a");
  CHECK(journal_file.channel_messages(0).sequence_number() == 2U);
  jewels::FactoryResult<InputQueue> queue_result;
  REQUIRE(jewels::ok(InputQueue::try_make(jewels::Out{queue_result}, journal_file, 0U)));
  std::vector<InputQueue::Execution> executions;
  const auto message = make_logged_message("channel_a", 2U);
  REQUIRE(jewels::ok(queue_result->push<ChannelA>(jewels::Out{executions}, message, make_test_message(102U))));

  REQUIRE(executions.size() == 1U);
  CHECK(executions.front().journal_execution.execution_index() == 2U);
  CHECK(queue_result->empty());
  CHECK(queue_result->stats() == make_stats(1U, 0U, 0U, 1U, 2U));
}

TEST_CASE("ExecutionQueue finalize releases inputs and classifies incomplete executions")
{
  JournalFile journal_file;
  auto* journal = journal_file.add_cog_journals();
  auto* first_execution = journal->add_executions();
  first_execution->set_execution_index(1U);
  add_input(journal_file, *first_execution, "channel_a", 1U, 2U);
  add_input(journal_file, *first_execution, "channel_b", 1U);
  auto* second_execution = journal->add_executions();
  second_execution->set_execution_index(2U);
  add_input(journal_file, *second_execution, "channel_a", 3U);
  add_input(journal_file, *second_execution, "channel_b", 2U);

  jewels::FactoryResult<Queue> queue_result;
  REQUIRE(jewels::ok(Queue::try_make(jewels::Out{queue_result}, journal_file, 0U)));
  std::vector<Execution> executions;
  auto final_stats = Tappy<ExecutionStats>{};

  SECTION("before any completion")
  {
    REQUIRE(jewels::ok(queue_result->finalize(jewels::Out{final_stats})));
    CHECK(final_stats == make_stats(2U, 0U, 0U, 0U, 2U));
  }

  SECTION("with available inputs for an incomplete execution")
  {
    auto message = make_logged_message("channel_a", 1U);
    REQUIRE(jewels::ok(queue_result->push<ChannelA>(jewels::Out{executions}, message, make_test_message(101U))));
    message = make_logged_message("channel_b", 1U);
    REQUIRE(jewels::ok(queue_result->push<QueueChannelB>(jewels::Out{executions}, message, make_other_message("b1"))));
    CHECK(executions.empty());
    CHECK(queue_result->buffered_message_count() == 2U);

    REQUIRE(jewels::ok(queue_result->finalize(jewels::Out{final_stats})));
    CHECK(final_stats == make_stats(2U, 0U, 0U, 0U, 2U));
  }

  SECTION("after a completion")
  {
    auto message = make_logged_message("channel_a", 1U);
    REQUIRE(jewels::ok(queue_result->push<ChannelA>(jewels::Out{executions}, message, make_test_message(101U))));
    message = make_logged_message("channel_b", 1U);
    REQUIRE(jewels::ok(queue_result->push<QueueChannelB>(jewels::Out{executions}, message, make_other_message("b1"))));
    message = make_logged_message("channel_a", 2U);
    REQUIRE(jewels::ok(queue_result->push<ChannelA>(jewels::Out{executions}, message, make_test_message(102U))));
    REQUIRE(executions.size() == 1U);
    check_execution(
      executions.front(),
      ExpectedExecution{
        .execution_index = 1U,
        .channel_a_messages =
          {
            {.sequence_number = 1U, .value = 101U},
            {.sequence_number = 2U, .value = 102U},
          },
        .channel_b_messages =
          {
            {.sequence_number = 1U, .value = "b1"},
          },
        .output_sequences = {},
      });

    REQUIRE(jewels::ok(queue_result->finalize(jewels::Out{final_stats})));
    CHECK(final_stats == make_stats(0U, 0U, 1U, 1U, 2U));
  }

  CHECK(queue_result->empty());
  CHECK(queue_result->buffered_message_count() == 0U);
  CHECK(queue_result->stats() == final_stats);
}

TEST_CASE("ExecutionQueue reconstructs configured channels absent from an execution")
{
  JournalFile journal_file;
  auto* execution = journal_file.add_cog_journals()->add_executions();
  add_input(journal_file, *execution, "channel_a", 1U);
  jewels::FactoryResult<Queue> queue_result;
  REQUIRE(jewels::ok(Queue::try_make(jewels::Out{queue_result}, journal_file, 0U)));
  std::vector<Execution> executions;

  const auto message = make_logged_message("channel_a", 1U);
  REQUIRE(jewels::ok(queue_result->push<ChannelA>(jewels::Out{executions}, message, make_test_message(101U))));
  REQUIRE(executions.size() == 1U);
  check_execution(
    executions.front(),
    ExpectedExecution{
      .execution_index = 0U,
      .channel_a_messages =
        {
          {.sequence_number = 1U, .value = 101U},
        },
      .channel_b_messages = {},
      .output_sequences = {},
    });
  const auto& absent_input = std::get<ReplayInput<QueueChannelB>>(executions.front().inputs);
  CHECK(absent_input.journal_state.channel_name() == "channel_b");
  CHECK(absent_input.messages.empty());
  const auto& absent_output = std::get<ReplayOutput<Output>>(executions.front().outputs);
  CHECK(absent_output.journal_state.channel_name() == "output");
  CHECK(absent_output.messages.empty());
}

TEST_CASE("ExecutionQueue rejects a channel missing from its type lists")
{
  JournalFile journal_file;
  auto* execution = journal_file.add_cog_journals()->add_executions();
  add_input(journal_file, *execution, "unknown", 1U);

  jewels::FactoryResult<Queue> queue_result;
  CHECK(jewels::fails(Queue::try_make(jewels::Out{queue_result}, journal_file, 0U)));
}

} // namespace
} // namespace clockwork::journal::replay
