// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/lite_compressor.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/onboard/clockwork_writer_policy.hh"
#include "clockwork/logging/onboard/writer.hh"
#include "clockwork/logging/realtime_playback/converter_request.hh"
#include "clockwork/logging/realtime_playback/converter_setup.hh"
#include "clockwork/serialization/cpp/tachyon_upgrader.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/shared_pool/ref_counted_pool.hh"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace clockwork_logging::realtime_playback
{

using ConverterWriter = onboard::Writer<onboard::ClockworkWriterPolicy<>>;

/// Recorded fields preserved in one converted message.
struct ConverterMessageView
{
  uint32_t sequence_number{};
  LogTimestamp publish_time;
  LogTimestamp log_time;
  std::span<const std::byte> header;
  std::span<const std::byte> data;
};

/// Upgrade one source payload while converting legacy upgrader exceptions to failure.
jewels::BinaryOutcome upgrade_converter_message(
  jewels::Out<std::vector<std::byte>> upgraded_out,
  const clockwork::serialization::TachyonUpgrader& upgrader,
  size_t target_message_size,
  std::span<const std::byte> source_data,
  std::string_view channel_name);

/// Reusable target upgrade, LiteCompression, and writer adapter.
class ConverterMessageWriter
{
public:
  explicit ConverterMessageWriter(jewels::memory::MemoryResource memory_resource);

  /// Convert and write one message while preserving its recorded metadata.
  template <typename Target>
  jewels::BinaryOutcome
  write(jewels::InOut<ConverterWriter> writer, const Target& target, const ConverterMessageView& message);

private:
  LiteCompressor compressor_;
  std::vector<std::byte> upgraded_;
};

/// One node-level converter writer and its relative output path.
struct ConverterNodeWriter
{
  std::string relative_path;
  std::unique_ptr<ConverterWriter> writer;
};

using ConverterNodeWriters = std::map<std::string, ConverterNodeWriter>;

/// Open node-level writers and register the selected channels.
template <typename NodeActiveCallable, typename AssignmentActiveCallable>
jewels::BinaryOutcome open_converter_node_writers(
  jewels::Out<ConverterNodeWriters> writers_out,
  const ConverterRequest& request,
  const ConverterSetup& setup,
  std::string_view stream_name,
  const NodeActiveCallable& node_active_callable,
  const AssignmentActiveCallable& assignment_active_callable);

/// Open one simulation-environment onboard writer.
jewels::BinaryOutcome open_converter_writer(
  jewels::Out<std::unique_ptr<ConverterWriter>> writer_out, std::string_view path, std::string_view log_name);

/// Register one Tachyon target channel on a converter writer.
template <typename Target>
jewels::BinaryOutcome add_converter_channel(jewels::InOut<ConverterWriter> writer, const Target& target);

/// Close and drain a converter writer.
jewels::BinaryOutcome close_converter_writer(jewels::InOut<ConverterWriter> writer) noexcept;

/// Best-effort close and drain for a map of converter writers.
template <typename Writers>
void cleanup_converter_writers(jewels::InOut<Writers> writers) noexcept;

/// Close and drain a map of converter writers, logging failures with the stream name.
template <typename Writers>
jewels::BinaryOutcome finalize_converter_writers(jewels::InOut<Writers> writers, std::string_view stream_name);

} // namespace clockwork_logging::realtime_playback

#include "clockwork/logging/realtime_playback/converter_writer.inl"
