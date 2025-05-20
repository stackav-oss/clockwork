// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/log_error.hh"
#include "jewels/memory/memory_resource.hh"

#include <memory_resource>
#include <regex>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace clockwork_logging::offboard
{

/// Helper class to load a log writer config from a text protobuf and
/// use the config to get the compression type and log file to use for
/// each channel written to the log.
///
/// The config is a set of rules with channel names or regex strings that
/// are used to match against the channel names. When a match is found the
/// compression type and file name prefix from the first rule that matched
/// the channel name is used for that channel.
///
/// The rules contain a file name prefix string. If this string is empty then
/// each matching channel is written to a separate file whose name is generated
/// from the channel name. If the prefix is not empty then all channels matching
/// the rule are written to the same file.
class WriterConfig
{
  /// Internal writer configuration rule representation
  struct ConfigRule
  {
    /// Compression type
    CompressionType compression_type;

    /// File name string
    std::pmr::string file_name_prefix;

    /// Regexes to match against the channel name
    std::pmr::vector<std::regex> matchers;
  };

public:
  /// Channel configuration obtained by applying the rules in the configuration
  /// against a channel name
  struct ChannelConfig
  {
    /// Compression type
    CompressionType compression_type;

    /// Log file name
    std::pmr::string file_name_prefix;

    /// Comparison operator
    /// @param[in] lhs Left hand operand
    /// @param[in] rhs Right hand operand
    /// @return True iff lhs == rhs
    [[nodiscard]] friend bool operator==(const ChannelConfig& lhs, const ChannelConfig& rhs) noexcept
    {
      return std::tie(lhs.compression_type, lhs.file_name_prefix) ==
             std::tie(rhs.compression_type, rhs.file_name_prefix);
    }
  };

  /// @param[in] memory_resource Memory resource
  explicit WriterConfig(jewels::memory::MemoryResource memory_resource);
  ~WriterConfig() noexcept = default;

  WriterConfig(const WriterConfig& other) = delete;
  WriterConfig& operator=(const WriterConfig& other) = delete;
  WriterConfig(WriterConfig&&) noexcept = default;
  WriterConfig& operator=(WriterConfig&&) noexcept = default;

  /// Set the writer configuration protobuf from a text proto
  /// @param[in] text_proto Text protobuf string
  [[nodiscard]] LogExpected<void> set_config_proto(std::string_view text_proto);

  /// Get the channel configuration for a channel name
  /// @param[in] channel_name Channel name
  /// @return Channel configuration to use for the channel
  [[nodiscard]] ChannelConfig get_channel_config(std::string_view channel_name) const;

private:
  /// Memory resource
  jewels::memory::MemoryResource memory_resource_;

  /// Writer configuration protobuf
  std::pmr::vector<ConfigRule> rules_;
};

} // namespace clockwork_logging::offboard
