// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/message_encoding.hh"
#include "clockwork/logging/readers/abstract_log_reader.hh"
#include "clockwork/logging/readers/log_reader_factory.hh"
#include "clockwork/logging/readers/types.hh"
#include "clockwork/logging/xxh3_checksum.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/std/expected.hh"

#include <tclap/CmdLine.h>
#include <tclap/UnlabeledValueArg.h>

#include <compare>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_set>
#include <utility>

namespace clockwork_logging
{

namespace
{

/// Struct used to defer comparing messages when two different messages have the same timestamp
struct DeferredLoggedMessage
{
  /// The topic the message was logged on.
  std::string topic;
  /// Sequence number
  uint32_t sequence_number;
  /// The publish time of the message.
  LogTimestamp publish_time;
  /// The log time of the message.
  LogTimestamp log_time;
  /// Header XXH3 checksum
  uint64_t header_checksum{};
  /// Data XXH3 checksum
  uint64_t data_checksum;
  /// Flag indicating a repeated persistent message published prior to the start of the log
  bool is_repeated_persistent;
  /// Message encoding
  MessageEncoding message_encoding;

  /// Constructor
  /// @param[in] msg Logged message
  explicit DeferredLoggedMessage(const LoggedMessage& msg);

  /// Equal operator
  bool operator==(const DeferredLoggedMessage&) const = default;

  /// Less than operator
  friend bool operator<(const DeferredLoggedMessage& lhs, const DeferredLoggedMessage& rhs)
  {
    return std::tie(
             lhs.topic,
             lhs.sequence_number,
             lhs.publish_time,
             lhs.log_time,
             lhs.header_checksum,
             lhs.data_checksum,
             lhs.is_repeated_persistent,
             lhs.message_encoding) <
           std::tie(
             rhs.topic,
             rhs.sequence_number,
             rhs.publish_time,
             rhs.log_time,
             rhs.header_checksum,
             rhs.data_checksum,
             rhs.is_repeated_persistent,
             rhs.message_encoding);
  }
};

DeferredLoggedMessage::DeferredLoggedMessage(const LoggedMessage& msg)
  : topic(msg.topic),
    sequence_number(msg.sequence_number),
    publish_time(msg.publish_time),
    log_time(msg.log_time),
    header_checksum(compute_xxh3_checksum(msg.header)),
    data_checksum(compute_xxh3_checksum(msg.data)),
    is_repeated_persistent(msg.is_repeated_persistent),
    message_encoding(msg.message_encoding)
{
}

/// Open a log for reading
/// @param[in] log_uri Log URI
/// @return Reader pointer or LogError on failure
[[nodiscard]] LogExpected<std::unique_ptr<AbstractLogReader>> open_reader(std::string_view log_uri)
{
  try
  {
    auto reader_ptr = make_reader(log_uri, {}, {});
    if (const auto open_result = reader_ptr->open({}); !open_result)
    {
      return jewels::unexpected(open_result.error());
    }
    return {std::move(reader_ptr)};
  }
  catch (const std::invalid_argument& exc)
  {
    jewels::log_cerr_error("{}", exc.what());
    return jewels::unexpected(LogError::failed_to_open_log_file);
  }
}

/// Validate the metadata for a topic
/// @param[in] reader1 Log reader to compare
/// @param[in] reader2 Log reader to compare
/// @param[in] topic Topic to validate
/// @param[in,out] validated_metadata Set of topics that have been validated
/// @return LogError on failure
[[nodiscard]] LogExpected<void> validate_metadata(
  AbstractLogReader& reader1,
  AbstractLogReader& reader2,
  std::string_view topic,
  std::unordered_set<std::string>& validated_metadata)
{
  const std::string topic_str{topic};
  if (validated_metadata.contains(topic_str))
  {
    return {};
  }
  const auto metadata1_result = reader1.get_channel_metadata(topic);
  if (!metadata1_result)
  {
    jewels::log_cerr_error("Failed to load metadata for {}: {}", topic, metadata1_result.error());
    return jewels::unexpected(LogError::missing_channel_metadata);
  }
  const auto metadata2_result = reader2.get_channel_metadata(topic);
  if (!metadata2_result)
  {
    jewels::log_cerr_error("Failed to load metadata for {}: {}", topic, metadata2_result.error());
    return jewels::unexpected(LogError::missing_channel_metadata);
  }
  if (metadata1_result.value() != metadata2_result.value())
  {
    jewels::log_cerr_error("Metadata mismatch for topic {}", topic);
    return jewels::unexpected(LogError::metadata_mismatch);
  }
  validated_metadata.emplace(topic_str);
  return {};
}

/// Process the next messages read from each log
/// @param[in] message1 Message to compare
/// @param[in] message2 Message to compare
/// @param[in,out] deferred_set1 Deferred comparison set
/// @param[in,out] deferred_set2 Deferred comparison set
[[nodiscard]] LogExpected<void> compare_messages(
  const LoggedMessage& message1,
  const LoggedMessage& message2,
  std::multiset<DeferredLoggedMessage>& deferred_set1,
  std::multiset<DeferredLoggedMessage>& deferred_set2)
{
  const DeferredLoggedMessage deferred1{message1};
  const DeferredLoggedMessage deferred2{message2};
  if (deferred1 != deferred2)
  {
    const auto deferred_iter1 = deferred_set1.find(deferred2);
    if (deferred_iter1 != deferred_set1.end())
    {
      deferred_set1.erase(deferred_iter1);
    }
    const auto deferred_iter2 = deferred_set2.find(deferred1);
    if (deferred_iter2 != deferred_set2.end())
    {
      deferred_set2.erase(deferred_iter2);
    }
  }
  return {};
}

/// Compare the messages and metadata from two logs
/// @param[in] reader1 Log reader to compare
/// @param[in] reader2 Log reader to compare
/// @return LogError on failure
[[nodiscard]] LogExpected<void> do_compare(AbstractLogReader& reader1, AbstractLogReader& reader2)
{
  std::multiset<DeferredLoggedMessage> deferred_set1;
  std::multiset<DeferredLoggedMessage> deferred_set2;
  std::unordered_set<std::string> validated_metadata;
  while (true)
  {
    const auto maybe_message1 = reader1.next_message();
    const auto maybe_message2 = reader2.next_message();
    if (!maybe_message1 && !maybe_message2)
    {
      break;
    }
    if (!maybe_message1 && maybe_message2)
    {
      jewels::log_cerr_error("Early end of log on reader1");
      return jewels::unexpected(LogError::failed);
    }
    if (maybe_message1 && !maybe_message2)
    {
      jewels::log_cerr_error("Early end of log on reader2");
      return jewels::unexpected(LogError::failed);
    }
    if (const auto compare_result = compare_messages(*maybe_message1, *maybe_message2, deferred_set1, deferred_set2);
        !compare_result)
    {
      return compare_result;
    }
    if (const auto validate_result = validate_metadata(reader1, reader2, maybe_message1->topic, validated_metadata);
        !validate_result)
    {
      return validate_result;
    }
    if (const auto validate_result = validate_metadata(reader1, reader2, maybe_message2->topic, validated_metadata);
        !validate_result)
    {
      return validate_result;
    }
  }
  if (!deferred_set1.empty())
  {
    jewels::log_cerr_error("Missing message on {} in log2", deferred_set1.begin()->topic);
    return jewels::unexpected(LogError::failed);
  }
  if (!deferred_set2.empty())
  {
    jewels::log_cerr_error("Missing message on {} in log1", deferred_set2.begin()->topic);
    return jewels::unexpected(LogError::failed);
  }
  return {};
}

/// Compare the contents of two logs
///
/// Succeeds if both logs return identical messages in the same order and the metadata
/// for every message found in the logs matches.
///
/// Metadata for channels not stored in the logs is only checked if the metadata appears in both
/// logs. Logs are not required to have metadata for messages that were never written to the log.
///
/// @param[in] log_uri1 Log to compare
/// @param[in] log_uri2 Log to compare
/// @return LogError on failure
[[nodiscard]] LogExpected<void> compare_logs(std::string_view log_uri1, std::string_view log_uri2)
{
  const auto reader1_result = open_reader(log_uri1);
  if (!reader1_result)
  {
    jewels::log_cerr_error("Failed to open {} for read: {}", log_uri1, reader1_result.error());
    return jewels::unexpected(reader1_result.error());
  }
  const auto reader2_result = open_reader(log_uri2);
  if (!reader2_result)
  {
    jewels::log_cerr_error("Failed to open {} for read: {}", log_uri2, reader2_result.error());
    return jewels::unexpected(reader2_result.error());
  }
  if (const auto compare_result = do_compare(*reader1_result.value(), *reader2_result.value()); !compare_result)
  {
    jewels::log_cerr_error("Failed to compare logs: {}", compare_result.error());
    return jewels::unexpected(compare_result.error());
  }
  return {};
}

} // namespace
} // namespace clockwork_logging

int main(int argc, char* argv[])
{
  try
  {
    TCLAP::CmdLine cmd("Compare logs", ' ', "1.0", true);
    const TCLAP::UnlabeledValueArg<std::string> lhs_log_uri_arg("lhs-log-uri", "Log URI", true, "", "uri", cmd);
    const TCLAP::UnlabeledValueArg<std::string> rhs_log_uri_arg("rhs-log-uri", "Log URI", true, "", "uri", cmd);

    cmd.parse(argc, argv);

    const auto& lhs_log_uri = lhs_log_uri_arg.getValue();
    const auto& rhs_log_uri = rhs_log_uri_arg.getValue();

    if (const auto compare_result = clockwork_logging::compare_logs(lhs_log_uri, rhs_log_uri); !compare_result)
    {
      jewels::log_cerr_error("Failed to compare logs: {}", compare_result.error());
      return 1;
    }
    std::cout << "\nSuccess\n\n";
    return 0;
  }
  catch (const std::exception& exc)
  {
    std::cerr << "Caught unexpected exception: " << exc.what() << '\n';
    return 1;
  }
}
