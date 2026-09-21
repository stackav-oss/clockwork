// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/decompress_option.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/offboard/s3_utils.hh"
#include "clockwork/logging/onboard/buffered_reader.hh"
#include "clockwork/logging/onboard/log_format.hh"
#include "clockwork/logging/onboard/offboard_buffered_reader.hh"
#include "clockwork/logging/onboard/reader.hh"
#include "clockwork/logging/readers/abstract_log_reader.hh"
#include "clockwork/logging/readers/types.hh"
#include "jewels/filesystem/filesystem.hh"
#include "jewels/math/constants.hh"
#include "jewels/memory/memory_resource.hh"

#include <cstddef>
#include <functional>
#include <memory>
#include <memory_resource>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace clockwork_logging
{

/// Onboard log reader policy
struct OnboardLogReaderPolicy
{
  /// S3 utility library type
  using S3UtilsType = offboard::S3Utils;

  /// Filesystem library type
  using FilesystemType = jewels::filesystem::Filesystem;

  /// Read buffer size
  static constexpr size_t read_buffer_size = 2U * jewels::math::constants::bytes_per_mib<size_t>;

  /// Maximum read size
  static constexpr size_t max_read_size = onboard::max_log_record_size;

  /// Minimum size of reads when recovering from I/O error
  static constexpr size_t min_io_error_recover_read_size = 512U;
};

namespace detail
{

/// Offboard reader implementation of the log reader interface
/// @tparam BufferedReaderType Buffered reader implementation type
template <typename BufferedReaderType>
class OnboardLogReaderImpl : public AbstractLogReader
{
public:
  /// Constructor
  /// @param[in] log_uri Log URI
  /// @param[in] maybe_log_interval The interval to read from the log
  /// @param[in] maybe_relative_interval The interval to read from the log relative to the sart of the log
  /// @param[in] decompress_option Option for whether to decompress lite-compressed messages found in the log
  /// @param[in] buffered_reader Buffered reader used to read log files
  OnboardLogReaderImpl(
    std::string_view log_uri,
    std::optional<LogInterval> maybe_log_interval,
    std::optional<RelativeInterval> maybe_relative_interval,
    DecompressOption decompress_option,
    std::shared_ptr<BufferedReaderType> buffered_reader);

  ~OnboardLogReaderImpl() override = default;
  OnboardLogReaderImpl(const OnboardLogReaderImpl&) = delete;
  OnboardLogReaderImpl& operator=(const OnboardLogReaderImpl&) = delete;
  OnboardLogReaderImpl(OnboardLogReaderImpl&&) = delete;
  OnboardLogReaderImpl& operator=(OnboardLogReaderImpl&&) = delete;

  /// Get the type of log reader
  /// @returns The log reader type
  [[nodiscard]] std::string type() const override;

  /// Open the log.
  /// @param[in] topic_filter Optional topic filter, return false if the topic should be ignored.
  ///                          if not set then all topics will be read.
  /// @returns expected with error set if there was an issue opening the log
  [[nodiscard]] LogExpected<void> open(const std::function<bool(std::string_view)>& topic_filter) override;

  /// Close the log.
  /// @returns expected with error set if there was an issue closing the log
  [[nodiscard]] LogExpected<void> close() override;

  /// Get the list of channel names from the log.
  /// @return vector of channel names
  [[nodiscard]] std::vector<std::string> get_channels() override;

  /// Get the metadata for all channels in the log
  /// @return vector of topic metadata
  [[nodiscard]] std::vector<TopicMetadata> get_metadata() override;

  /// Get the metadata for a single channel
  /// @return Topic metadata or LogError on failure
  [[nodiscard]] LogExpected<TopicMetadata> get_channel_metadata(std::string_view channel_name) override;

  /// Get the log start time
  /// @return the log start time or LogError on failure
  [[nodiscard]] LogExpected<LogTimestamp> start_time() override;

  /// Get the log end time
  /// @return the log end time or LogError on failure
  [[nodiscard]] LogExpected<LogTimestamp> end_time() override;

  /// Read the next message out of the log.
  /// @note The logged message is only valid until the next call of next_message or close.
  /// @returns next message if there are any left, otherwise nullopt
  [[nodiscard]] std::optional<LoggedMessage> next_message_impl() override;

  /// Read the next message out of the log with zero copy
  /// @note The logged message is only valid until the next call of next_message or close.
  /// @returns next message if there are any left, otherwise nullopt
  [[nodiscard]] std::optional<ZeroCopyLoggedMessage> zero_copy_next_message_impl() override;

private:
  /// Read the log and load the topic metadata
  void load_topic_metadata();

  /// Memory resource
  jewels::memory::MemoryResource memory_resource_;

  /// Log reader
  onboard::Reader<BufferedReaderType> reader_;

  /// Topic metadata pointer
  std::unique_ptr<std::vector<TopicMetadata>> topic_metadata_ptr_;

  /// First message timestamp
  LogTimestamp first_message_timestamp_;

  /// Last message timestamp
  LogTimestamp last_message_timestamp_;

  /// Interval to be read
  LogInterval log_interval_;

  /// Topic filter
  std::function<bool(std::string_view)> topic_filter_;

  /// Flag set when the reader is open
  bool is_open_{false};

  /// Option for whether to decompress lite-compressed messages found in the log
  DecompressOption decompress_option_;

  /// Buffered reader
  std::shared_ptr<BufferedReaderType> buffered_reader_;
};

} // namespace detail

using OnboardBufferedReader = onboard::BufferedReader<OnboardLogReaderPolicy>;
using OnboardLogReader = detail::OnboardLogReaderImpl<OnboardBufferedReader>;
using OffboardOnboardBufferedReader = onboard::OffboardBufferedReader<OnboardLogReaderPolicy>;
using OffboardOnboardLogReader = detail::OnboardLogReaderImpl<OffboardOnboardBufferedReader>;

} // namespace clockwork_logging

#include "clockwork/logging/readers/onboard_log_reader.inl"
