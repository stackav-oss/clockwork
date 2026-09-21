// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/journal/journal.pb.h"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace clockwork::journal::replay
{

/// Identifies an input message within a channel by sequence number.
using MessageKey = uint64_t;

/// A message and the number of execution input views that reference it.
struct MessageRequirement
{
  /// Message identifier.
  MessageKey key{};
  /// Number of execution input views that reference the message.
  size_t reference_count{};
};

/// All required messages for one input channel.
struct ChannelRequirements
{
  /// Logged channel name.
  std::string channel_name;
  /// Required messages, ordered by sequence number.
  std::vector<MessageRequirement> messages;
};

/// Messages required from one input channel for an execution.
struct ExecutionInput
{
  /// Journal input-view state.
  InputViewState journal_state;
  /// Required message identifiers in input-view order.
  std::vector<MessageKey> messages;
};

/// Expected messages on one output channel for an execution.
struct ExecutionOutput
{
  /// Journal output state.
  OutputState journal_state;
  /// Expected output metadata in production order.
  std::vector<ChannelMessage> messages;
};

/// Metadata needed to reconstruct one execution cycle.
struct ExecutionMetadata
{
  /// Journal execution state.
  CogExecution journal_execution;
  /// Required input messages.
  std::vector<ExecutionInput> inputs;
  /// Expected output messages.
  std::vector<ExecutionOutput> outputs;
};

/// Prepared replay metadata for one cog.
struct ReplayMetadata
{
  /// Required messages and reference counts, grouped by input channel.
  std::vector<ChannelRequirements> input_channels;
  /// Execution cycles in journal order.
  std::vector<ExecutionMetadata> executions;
};

/// Prepare validated replay metadata for one cog.
/// @param metadata Prepared replay metadata on success.
/// @param journal_file Journal containing cog executions and channel message metadata.
/// @param cog_journal_index Index of the cog journal to prepare.
/// @return Failure when the cog index is invalid, an input view has invalid sequence metadata, channel message metadata
/// is duplicated, or required output message metadata is missing.
jewels::BinaryOutcome
make_replay_metadata(jewels::Out<ReplayMetadata> metadata, const JournalFile& journal_file, size_t cog_journal_index);

} // namespace clockwork::journal::replay
