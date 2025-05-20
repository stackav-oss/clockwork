// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/container/tap/var_array.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/meta/concepts.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <google/protobuf/duration.pb.h>
#include <google/protobuf/timestamp.pb.h>

#include <chrono>
#include <cstddef>
#include <span>
#include <string_view>

namespace jewels
{
using ConversionStatusExpected = jewels::expected<void, jewels::MonoError>;
// Convert from protobuf string to a Tap UUID
template <typename Tag>
ConversionStatusExpected protobuf_to_tap(jewels::Uuid<Tag>& output, std::string_view input);

// Convert from a protobuf string to a Tap VarString
template <size_t capacity>
ConversionStatusExpected protobuf_to_tap(tap::VarString<capacity>& output, std::string_view input);

// Convert from a protobuf timestamp to SyncTime
ConversionStatusExpected protobuf_to_tap(jewels::time::SyncTime& output, const google::protobuf::Timestamp& input);

// Convert from protobuf durations to std::chrono::nanoseconds
ConversionStatusExpected protobuf_to_tap(std::chrono::nanoseconds& output, const google::protobuf::Duration& input);

// Convert from protobuf string to a single Tap byte
ConversionStatusExpected protobuf_to_tap(std::byte& output, std::string_view input);

// Convert from a protobuf string to a tap VarArray of bytes
template <size_t capacity>
ConversionStatusExpected protobuf_to_tap(tap::VarArray<std::byte, capacity>& output, std::string_view input);

template <size_t size>
ConversionStatusExpected protobuf_to_tap(std::span<std::byte, size> output, std::string_view input);

// Handle integer conversion, needed because protobuf doesn't support ints smaller than 32b
template <jewels::meta::Integral From, jewels::meta::Integral To>
ConversionStatusExpected protobuf_to_tap(To& output, From input);

} // namespace jewels

#include "jewels/container/tap/protobuf_to_tap.inl"
