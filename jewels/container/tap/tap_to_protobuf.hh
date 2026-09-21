// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/container/tap/var_array.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <google/protobuf/duration.pb.h>
#include <google/protobuf/timestamp.pb.h>

#include <chrono>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace jewels
{

// Convert from a Tap UUID to protobuf string
template <typename Tag>
void tap_to_protobuf(std::string& output, const Uuid<Tag>& input);

// Convert from a Tap VarString to a protobuf string
template <size_t capacity>
void tap_to_protobuf(std::string& output, const tap::VarString<capacity>& input);

// Convert from SyncTime to a protobuf timestamp
void tap_to_protobuf(google::protobuf::Timestamp& output, const time::SyncTime& input);

// Convert from std::chrono::nanoseconds to protobuf durations
void tap_to_protobuf(google::protobuf::Duration& output, const std::chrono::nanoseconds& input);

// Convert from a single Tap byte to protobuf string
void tap_to_protobuf(std::string& output, std::byte input);

// Convert from a tap VarArray of bytes to a protobuf string
template <size_t capacity>
void tap_to_protobuf(std::string& output, const tap::VarArray<std::byte, capacity>& input);

// Convert from a fixed sized array of bytes to a protobuf string
template <size_t size>
void tap_to_protobuf(std::string& output, std::span<const std::byte, size> input);

// Convert from a string_view to a protobuf string
void tap_to_protobuf(std::string& output, std::string_view input);

} // namespace jewels
#include "jewels/container/tap/tap_to_protobuf.inl"
