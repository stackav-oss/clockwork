// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/journal/journal.pb.h"
#include "clockwork/journal/replay/execution_stats_clk_cc.hh"
#include "clockwork/journal/replay/message_buffer.hh"
#include "clockwork/journal/replay/replay_metadata.hh"
#include "clockwork/logging/readers/types.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/meta/call.hh"
#include "jewels/meta/type_traits.hh"
#include "jewels/meta/types.hh"

#include <concepts>
#include <cstddef>
#include <memory>
#include <set>
#include <string_view>
#include <tuple>
#include <vector>

namespace clockwork::journal::replay
{

/// A replay channel policy supplies its message type and logged channel name.
template <typename Channel>
concept ReplayChannel = requires {
  typename Channel::MsgType;
  requires clockwork::TappyType<typename Channel::MsgType>;
  { Channel::name } -> std::convertible_to<std::string_view>;
};

/// A non-empty pack of unique replay channel policy types.
template <typename... Channels>
concept ReplayChannelTypePack = sizeof...(Channels) > 0U && (ReplayChannel<Channels> && ...) &&
                                jewels::meta::Call<jewels::meta::UniqueT, Channels...>::value;

/// Typed messages for one input channel in a reconstructed execution.
template <ReplayChannel Channel>
struct ReplayInput
{
  /// Journal input-view state.
  InputViewState journal_state;
  /// Input messages in journal order.
  std::vector<std::shared_ptr<const ReplayMessage<typename Channel::MsgType>>> messages;
};

/// Expected output metadata for one output channel in a reconstructed execution.
template <ReplayChannel Channel>
struct ReplayOutput
{
  /// Journal output state.
  OutputState journal_state;
  /// Expected message headers in production order.
  std::vector<ChannelMessage> messages;
};

/// Return whether exactly one channel has the name.
/// @tparam Channels Channel policies to inspect.
/// @param channel_name Logged channel name to find.
template <ReplayChannel... Channels>
[[nodiscard]] bool has_unique_channel_name(std::string_view channel_name);

/// Return whether every metadata channel occurs once and matches the configured channel types.
/// @tparam InputChannels Configured input channel policies.
/// @tparam OutputChannels Configured output channel policies.
/// @param input_channels Configured input channel type list.
/// @param output_channels Configured output channel type list.
/// @param metadata Replay metadata to validate.
template <ReplayChannel... InputChannels, ReplayChannel... OutputChannels>
[[nodiscard]] bool metadata_channels_match_configuration(
  jewels::meta::Types<InputChannels...> input_channels,
  jewels::meta::Types<OutputChannels...> output_channels,
  const ReplayMetadata& metadata);

/// Copy one typed input from an execution.
/// @tparam Channel Input channel to copy.
/// @param input Reconstructed typed input on success.
/// @param execution Execution metadata containing the input requirements.
/// @param buffer Typed input message buffer.
/// @return Failure if a required input message is not buffered.
template <ReplayChannel Channel>
jewels::BinaryOutcome copy_inputs(
  jewels::Out<ReplayInput<Channel>> input,
  const ExecutionMetadata& execution,
  const MessageBuffer<typename Channel::MsgType>& buffer);

/// Copy expected metadata for one typed output from an execution.
/// @tparam Channel Output channel to copy.
/// @param output Reconstructed typed output metadata.
/// @param execution Execution metadata containing the expected outputs.
template <ReplayChannel Channel>
void copy_outputs(jewels::Out<ReplayOutput<Channel>> output, const ExecutionMetadata& execution);

/// Rebuild the missing-message set for one input channel in an execution.
/// @tparam Channel Input channel whose missing messages are refreshed.
/// @param missing_messages Missing sequence numbers after the refresh.
/// @param execution Execution containing the input requirements.
/// @param buffer Typed message buffer containing available inputs.
template <ReplayChannel Channel>
void refresh_missing_messages(
  jewels::Out<std::set<MessageKey>> missing_messages,
  const ExecutionMetadata& execution,
  const MessageBuffer<typename Channel::MsgType>& buffer);

/// Consume every input reference held by an execution.
/// @tparam InputChannels Configured input channel policies in buffer parameter order.
/// @param buffers Typed input message buffers. Buffers may be partially consumed on failure.
/// @param execution Execution whose input references are consumed.
/// @return Failure if an execution input channel is not configured or a reference cannot be consumed.
template <ReplayChannel... InputChannels>
jewels::BinaryOutcome consume_execution_references(
  jewels::InOut<MessageBuffer<typename InputChannels::MsgType>>... buffers, const ExecutionMetadata& execution);

/// Reconstructed execution cycle, specialized on input and output channel type lists.
template <typename InputChannels, typename OutputChannels>
struct ReplayExecution;

template <ReplayChannel... InputChannels, ReplayChannel... OutputChannels>
struct ReplayExecution<jewels::meta::Types<InputChannels...>, jewels::meta::Types<OutputChannels...>>
{
  /// Journal execution state.
  CogExecution journal_execution;
  /// Typed inputs in input type-list order.
  std::tuple<ReplayInput<InputChannels>...> inputs;
  /// Expected outputs in output type-list order.
  std::tuple<ReplayOutput<OutputChannels>...> outputs;
};

/// Buffers typed log messages and returns complete journaled executions in order.
template <typename InputChannels, typename OutputChannels>
class ExecutionQueue;

template <ReplayChannel... InputChannels, ReplayChannel... OutputChannels>
class ExecutionQueue<jewels::meta::Types<InputChannels...>, jewels::meta::Types<OutputChannels...>>
{
  static_assert(ReplayChannelTypePack<InputChannels...>, "Input channel types must be non-empty and unique");
  static_assert(ReplayChannelTypePack<OutputChannels...>, "Output channel types must be non-empty and unique");

private:
  template <ReplayChannel Channel>
  struct ChannelBuffer
  {
    /// Construct a channel buffer from validated requirements.
    /// @param channel_buffer Constructed channel buffer on success.
    /// @param requirements Message requirements for all input channels.
    /// @return Failure if the requirements for this message type are invalid.
    static jewels::BinaryOutcome
    try_make(jewels::FactoryOut<ChannelBuffer> channel_buffer, const std::vector<ChannelRequirements>& requirements);

    MessageBuffer<typename Channel::MsgType> buffer;

  private:
    /// Construct a channel buffer around an initialized message buffer.
    explicit ChannelBuffer(MessageBuffer<typename Channel::MsgType> message_buffer);
  };

  template <ReplayChannel Channel>
  struct CurrentInputState
  {
    std::set<MessageKey> missing_messages;
  };

  using ChannelBuffers = std::tuple<ChannelBuffer<InputChannels>...>;
  using CurrentInputStates = std::tuple<CurrentInputState<InputChannels>...>;

public:
  using InputChannelTypes = jewels::meta::Types<InputChannels...>;
  using OutputChannelTypes = jewels::meta::Types<OutputChannels...>;
  using Execution = ReplayExecution<InputChannelTypes, OutputChannelTypes>;

  /// Construct a validated queue for one cog in a journal file.
  /// @param queue Constructed queue on success.
  /// @param journal_file Journal containing the cog and indexed message metadata.
  /// @param cog_journal_index Index of the cog journal to reconstruct.
  /// @return Failure if replay metadata is invalid or a journal channel is absent from the corresponding type list.
  static jewels::BinaryOutcome
  try_make(jewels::FactoryOut<ExecutionQueue> queue, const JournalFile& journal_file, size_t cog_journal_index);

  /// Add a typed message from its log-reader callback and return all newly complete executions.
  ///
  /// The output is cleared before processing. A message newer than a missing input causes incomplete executions to be
  /// discarded until the queue can still complete its next execution.
  /// @tparam Channel Input channel policy associated with the callback.
  /// @param executions Newly complete executions in journal order.
  /// @param logged_message Raw logged message metadata.
  /// @param message Deserialized message payload.
  /// @return Failure if the callback channel does not match Channel or internal replay metadata is inconsistent.
  template <ReplayChannel Channel>
    requires(std::same_as<Channel, InputChannels> || ...)
  jewels::BinaryOutcome push(
    jewels::Out<std::vector<Execution>> executions,
    const clockwork_logging::LoggedMessage& logged_message,
    const typename Channel::MsgType& message);

  /// Finalize replay at the end of the log.
  ///
  /// All remaining executions are counted as dropped.
  /// @param stats Final execution reconstruction statistics.
  /// @return Failure if internal replay metadata is inconsistent.
  jewels::BinaryOutcome finalize(jewels::Out<clockwork::Tappy<ExecutionStats>> stats);

  /// Return true after every journal execution was completed or dropped.
  [[nodiscard]] bool empty() const noexcept;

  /// Return execution reconstruction statistics.
  [[nodiscard]] const clockwork::Tappy<ExecutionStats>& stats() const noexcept;

  /// Return the total number of buffered typed messages.
  [[nodiscard]] size_t buffered_message_count() const noexcept;

private:
  /// Construct a queue from extracted replay metadata and initialized channel buffers.
  /// @param metadata Replay metadata for the selected cog.
  /// @param buffers Typed input-channel buffers.
  explicit ExecutionQueue(ReplayMetadata metadata, ChannelBuffers buffers);

  /// Append every currently ready execution to the output.
  /// @param executions Complete executions in journal order.
  /// @return Failure if internal replay metadata is inconsistent.
  jewels::BinaryOutcome process_ready(jewels::Out<std::vector<Execution>> executions);

  /// Rebuild the missing-message sets for the current execution.
  void refresh_current_execution_state();

  /// Return whether every input required by the current execution is buffered.
  [[nodiscard]] bool next_execution_is_ready() const;

  /// Return whether a pushed message proves that the current execution missed an earlier input.
  /// @tparam Channel Input channel associated with the pushed message.
  /// @param pushed_key Sequence number of the pushed message.
  template <ReplayChannel Channel>
  [[nodiscard]] bool pushed_message_skips_next_execution(const MessageKey& pushed_key) const;

  /// Discard the current execution and consume its message references.
  /// @return Failure if the queue is empty or an input reference cannot be consumed.
  jewels::BinaryOutcome discard_next_execution();

  /// Build and consume the current execution.
  /// @param execution Reconstructed execution on success.
  /// @return Failure if the queue is empty, an input message cannot be copied, or an input reference cannot be
  /// consumed.
  jewels::BinaryOutcome assemble_and_consume_next_execution(jewels::Out<Execution> execution);

  /// Record an execution discarded while processing input messages.
  void record_stream_drop();

  ReplayMetadata metadata_;
  size_t next_execution_index_{};
  clockwork::Tappy<ExecutionStats> stats_;
  ChannelBuffers buffers_;
  CurrentInputStates current_input_states_;
};

} // namespace clockwork::journal::replay

#include "clockwork/journal/replay/execution_queue.inl"
