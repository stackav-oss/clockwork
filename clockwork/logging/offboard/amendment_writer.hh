// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/offboard/s3_utils.hh"
#include "clockwork/logging/offboard/types.hh"
#include "clockwork/logging/offboard/writer.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/memory/memory_resource.hh"

#include <string_view>

namespace clockwork_logging::offboard
{

/// Offboard log amendment writer
/// @tparam S3UtilsType S3 utility helper class type
template <typename S3UtilsType = S3Utils>
class AmendmentWriter
{
public:
  /// Header for the log amendment metadata text protobuf file
  static constexpr auto log_amendment_proto_header =
    "# proto-file: clockwork/logging/offboard/v1/log_amendment.proto\n# proto-message: LogAmendment\n";

  /// Constructor
  /// @param[in] memory_resource Memory resource
  explicit AmendmentWriter(
    jewels::memory::MemoryResource memory_resource,
    MessageChunkIndexFormat message_chunk_index_format = MessageChunkIndexFormat::v2);

  ~AmendmentWriter() = default;

  AmendmentWriter(const AmendmentWriter&) = delete;
  AmendmentWriter& operator=(const AmendmentWriter&) = delete;
  AmendmentWriter(AmendmentWriter&&) = delete;
  AmendmentWriter& operator=(AmendmentWriter&&) = delete;

  /// Open the amendment writer
  /// @param[in] amendment_uri Amendment log URI
  /// @param[in] amended_path Absolute or relative path from the amendedment log to the amended log (i.e. "../event")
  /// @param[in] config_str Writer config text proto string
  /// @param[in] overwrite_mode Open overwrite mode
  /// @return Success or LogError on failure
  LogOutcome open(
    std::string_view amendment_uri,
    std::string_view amended_path,
    std::string_view config_str = {},
    OverwriteMode overwrite_mode = OverwriteMode::dont_overwrite);

  /// Close the amendmentlog
  /// @return Success or LogError on failure
  LogOutcome close();

  /// Add an amended channel to the log
  ///
  /// The contents of the amended channel replace any messages on the same channel
  /// in the amended log.
  ///
  /// @param[in] channel_metadata Logged channel metadata
  /// @return Success or LogError on failure
  LogOutcome create_channel(const LoggedChannelMetadata& channel_metadata);

  /// Add an amended clockwork channel to the log, metadata is deduced from logging traits
  ///
  /// The contents of the amended channel replace any messages on the same channel
  /// in the amended log.
  ///
  /// @tparam T Channel message type
  /// @param[in] channel_name Channel name
  template <clockwork::TappyType T>
  LogOutcome create_channel(std::string_view channel_name, ChannelType channel_type = ChannelType::regular);

  /// Write a message to the log
  /// @param[in] message Message to log
  /// @return Success or LogError on failure
  LogOutcome write(const LoggedMessage& message);

  /// Write a message to the log
  /// @param[in] message Message to log
  /// @return Success or LogError on failure
  LogOutcome write(const ZeroCopyLoggedMessage& message);

  /// Write a tachyon message to the log
  /// @tparam T Tachyon message type
  /// @param[in] channel_name Channel name
  /// @param[in] sequence_number Sequence number
  /// @param[in] log_time Message log time
  /// @param[in] transmit_time Message transmit time
  /// @param[in] message Message to log
  /// @param[in] is_repeated_persistent Flag indicating a repeated persistent channel message from a prior log
  /// @return Success or LogError on failure
  template <clockwork::TappyType T>
  LogOutcome write(
    std::string_view channel_name,
    uint32_t sequence_number,
    LogTimestamp log_time,
    LogTimestamp transmit_time,
    const T& message,
    bool is_repeated_persistent = false);

private:
  /// Memory resource
  jewels::memory::MemoryResource memory_resource_;

  /// Chunk writer factory
  ChunkReaderWriterFactory<S3UtilsType> chunk_writer_factory_;

  /// Writer instance
  Writer<S3UtilsType> writer_;

  /// Amendment log URI string
  std::pmr::string amendment_uri_str_;

  /// Amended log path string (may be either an absolute or a relative path)
  std::pmr::string amended_path_str_;

  /// Set of channels in the amendment log
  std::pmr::unordered_set<std::pmr::string> amended_channels_;
};

} // namespace clockwork_logging::offboard

#include "clockwork/logging/offboard/amendment_writer.inl"
