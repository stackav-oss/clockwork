// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/offboard/writer_config.hh"

#include "clockwork/logging/offboard/v1/writer_config.pb.h"
#include "jewels/std/expected.hh"

#include <google/protobuf/repeated_ptr_field.h>
#include <google/protobuf/text_format.h>

#include <cstddef>
#include <memory_resource>
#include <mutex>
#include <utility>
#include <vector>

namespace clockwork_logging::offboard
{

namespace
{

/// Default log file name
constexpr auto default_log_file_name_prefix = "other_channels";

/// Default compression type
constexpr auto default_compression_type = CompressionType::zstd;

/// Generate a log file name prefix from a channel name
/// @param[in] memory_resource Memory resource
/// @param[in] channel_name Channel name
/// @return Log file name prefix
[[nodiscard]] std::pmr::string
get_log_file_name_prefix(const jewels::memory::MemoryResource& memory_resource, std::string_view channel_name)
{
  std::pmr::string file_name_prefix{channel_name, memory_resource};
  for (auto& chr : file_name_prefix)
  {
    if (chr == '/')
    {
      chr = '_';
    }
  }
  return file_name_prefix;
}

} // namespace

WriterConfig::WriterConfig(jewels::memory::MemoryResource memory_resource)
  : memory_resource_(std::move(memory_resource)), rules_(memory_resource_)
{
}

[[nodiscard]] LogExpected<void> WriterConfig::set_config_proto(std::string_view text_proto)
{
  /// Mutex to prevent data races in set_writer_config_proto
  /// TSAN fails because of data races deep in the regex code.
  static std::mutex set_config_proto_mutex;
  const std::scoped_lock guard{set_config_proto_mutex};

  const std::string proto_str{text_proto};
  clockwork::logging::offboard::v1::WriterConfig config_proto;
  if (!google::protobuf::TextFormat::ParseFromString(proto_str, &config_proto))
  {
    return jewels::unexpected(LogError::invalid_protobuf_file);
  }
  rules_.clear();
  rules_.reserve(static_cast<size_t>(config_proto.rule().size()));
  for (const auto& rule : config_proto.rule())
  {
    std::pmr::string file_name_prefix{rule.file_name_prefix(), memory_resource_};
    rules_.push_back(
      ConfigRule{
        .compression_type = rule.compression_type() == clockwork::logging::offboard::v1::COMPRESSION_TYPE_NONE
                              ? CompressionType::none
                              : CompressionType::zstd,
        .file_name_prefix = std::move(file_name_prefix),
        .matchers = std::pmr::vector<std::regex>{memory_resource_},
      });
    auto& matchers = rules_.back().matchers;
    matchers.reserve(static_cast<size_t>(rule.regex().size()));
    for (const auto& regex : rule.regex())
    {
      matchers.emplace_back(regex);
    }
  }
  return {};
}

[[nodiscard]] WriterConfig::ChannelConfig WriterConfig::get_channel_config(std::string_view channel_name) const
{
  for (const auto& rule : rules_)
  {
    for (const auto& matcher : rule.matchers)
    {
      const std::pmr::string channel_name_str{channel_name, memory_resource_};
      if (std::regex_match(channel_name_str.c_str(), matcher))
      {
        return ChannelConfig{
          .compression_type = rule.compression_type,
          .file_name_prefix = rule.file_name_prefix.empty() ? get_log_file_name_prefix(memory_resource_, channel_name)
                                                            : std::pmr::string{rule.file_name_prefix, memory_resource_},
        };
      }
    }
  }
  return ChannelConfig{
    .compression_type = default_compression_type,
    .file_name_prefix = std::pmr::string{default_log_file_name_prefix, memory_resource_},
  };
}

} // namespace clockwork_logging::offboard
