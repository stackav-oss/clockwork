// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/container/tap/protobuf_to_tap.hh"

#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/time/conversions.hh"
#include "jewels/time/sync_time.hh"

#include <google/protobuf/duration.pb.h>
#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/util/time_util.h>

namespace jewels
{
ConversionStatusExpected protobuf_to_tap(jewels::time::SyncTime& output, const google::protobuf::Timestamp& input)
{
  output = jewels::time::sync_time_from_ns(google::protobuf::util::TimeUtil::TimestampToNanoseconds(input));
  return ConversionStatusExpected{};
}

ConversionStatusExpected protobuf_to_tap(std::chrono::nanoseconds& output, const google::protobuf::Duration& input)
{
  output = std::chrono::nanoseconds{google::protobuf::util::TimeUtil::DurationToNanoseconds(input)};
  return ConversionStatusExpected{};
}

ConversionStatusExpected protobuf_to_tap(std::byte& output, std::string_view input)
{
  if (input.size() > 1)
  {
    jewels::log_cerr_error(
      "The corresponding clockwork field for byte input only can "
      "contain a single character however the input string was {}",
      input);

    return jewels::unexpected(jewels::MonoError{});
  }
  if (input.empty())
  {
    return ConversionStatusExpected{};
  }
  output = static_cast<std::byte>(input[0]);
  return ConversionStatusExpected{};
}
} // namespace jewels
