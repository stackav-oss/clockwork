// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/pinion/slot_ref.hh"
#include "jewels/memory/pointers.hh"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>

namespace clockwork::tools
{

/// Raw message callback function
/// @param[in] sequence_number Message sequence number
/// @param[in] message_time Message time in nanoseconds since start of epoch
/// @param[in] data Raw message data
/// @param[in] overrun_check_fn Returns true if the message data is still valid
using RawMessageCallback = std::function<void(
  uint64_t sequence_number,
  int64_t message_time,
  std::span<const std::byte> data,
  const std::function<bool()>& overrun_check_fn)>;

/// Deserialized message callback function
/// @tparam MessageType
/// @param[in] sequence_number Message sequence number
/// @param[in] message_time Message time in nanoseconds since start of epoch
/// @param[in] message_ptr Deserialized message pointer
template <typename MessageType>
using DeserializedMessageCallback =
  std::function<void(uint64_t sequence_number, int64_t message_time, std::unique_ptr<MessageType> message_ptr)>;

/// Callback python handle used to pass raw messages into python
struct PythonCallbackHandle
{
public:
  /// Constructor
  /// @param[in] sequence_number Message sequence number
  /// @param[in] message_time Message time in nanoseconds since start of epoch
  /// @param[in] data Raw message data
  /// @param[in] subscriber_handle Subscriber handle
  /// @param[in] buffer_iter Pinion buffer iterator
  PythonCallbackHandle(
    uint64_t sequence_number_in,
    int64_t message_time_in,
    std::span<const std::byte> data_in,
    pinion::SlotRef slot_ref_in);

  ~PythonCallbackHandle() noexcept = default;
  PythonCallbackHandle(const PythonCallbackHandle&) = default;
  PythonCallbackHandle& operator=(const PythonCallbackHandle&) = default;
  PythonCallbackHandle(PythonCallbackHandle&&) = delete;
  PythonCallbackHandle& operator=(PythonCallbackHandle&&) = delete;

  /// Sequence number
  uint64_t sequence_number;

  /// Message time in nanoseconds since start of epoch
  int64_t message_time;

  /// Message data pointer
  jewels::memory::ObjectPtr<const std::byte> data_ptr;

  /// Message data size
  size_t data_size;

  /// Pinion buffer iterator
  pinion::SlotRef slot_ref;
};

/// Raw message callback function
/// @param[in] sequence_number Message sequence number
/// @param[in] message_time Message time in nanoseconds since start of epoch
/// @param[in] data Raw message data
/// @param[in] overrun_check_fn Returns true if the message data is still valid
using PythonCallback = std::function<void(const PythonCallbackHandle& callback_handle)>;

} // namespace clockwork::tools
