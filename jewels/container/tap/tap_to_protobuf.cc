// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/container/tap/tap_to_protobuf.hh"

#include "jewels/time/conversions.hh"

#include <google/protobuf/duration.pb.h>
#include <google/protobuf/timestamp.pb.h>

#include <chrono>
#include <cstdint>

namespace jewels
{

void tap_to_protobuf(google::protobuf::Timestamp& output, const time::SyncTime& input)
{
  auto ns_since_epoch = time::get_ns(input);
  auto seconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::nanoseconds(ns_since_epoch));
  auto nanos = std::chrono::nanoseconds(ns_since_epoch) - seconds;

  output.set_seconds(seconds.count());
  output.set_nanos(static_cast<int32_t>(nanos.count()));
}

void tap_to_protobuf(google::protobuf::Duration& output, const std::chrono::nanoseconds& input)
{
  auto total_seconds = std::chrono::duration_cast<std::chrono::seconds>(input);
  auto nanos = input - total_seconds;

  output.set_seconds(total_seconds.count());
  output.set_nanos(static_cast<int32_t>(nanos.count()));
}

void tap_to_protobuf(std::string& output, std::byte input)
{
  output = std::string(1, static_cast<char>(input));
}

void tap_to_protobuf(std::string& output, std::string_view input)
{
  output.assign(input);
}

} // namespace jewels
