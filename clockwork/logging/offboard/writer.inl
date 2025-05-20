// IWYU pragma: private, include "clockwork/logging/offboard/writer.hh"
#pragma once

#include "clockwork/logging/offboard/writer.hh"

#include "clockwork/logging/channel_type.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/offboard/types.hh"
#include "clockwork/repr_iface.hh"

#include <cstdint>
#include <span>
#include <string_view>

namespace clockwork_logging::offboard
{

template <clockwork::TappyType T>
[[nodiscard]] LogExpected<void> Writer::create_channel(std::string_view channel_name, ChannelType channel_type)
{
  return create_channel(LoggedChannelMetadata{
    .channel_name = channel_name,
    .message_encoding = clockwork::LoggingTraits<T>::message_encoding,
    .channel_type = channel_type,
    .schema_name = clockwork::LoggingTraits<T>::schema_name,
    .schema_encoding = clockwork::LoggingTraits<T>::schema_encoding,
    .schema_definition =
      std::string_view{
        clockwork::LoggingTraits<T>::schema_definition.data(), clockwork::LoggingTraits<T>::schema_definition.size()},
  });
}

template <clockwork::TappyType T>
[[nodiscard]] LogExpected<void> Writer::write(
  std::string_view channel_name,
  uint32_t sequence_number,
  LogTimestamp log_time,
  LogTimestamp transmit_time,
  const T& message,
  bool is_repeated_persistent)
{
  const auto data_span = std::as_bytes(std::span{&message, 1U});
  return write(ZeroCopyLoggedMessage{
    .channel_name = channel_name,
    .sequence_number = sequence_number,
    .log_time = log_time,
    .transmit_time = transmit_time,
    .header = {},
    .data = {&data_span, 1U},
    .is_repeated_persistent = is_repeated_persistent,
    .message_encoding = clockwork::LoggingTraits<T>::message_encoding,
  });
}

} // namespace clockwork_logging::offboard
