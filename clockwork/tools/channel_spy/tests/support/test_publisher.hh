// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/pinion/publisher_slot_ref.hh"
#include "clockwork/pinion/shm_publisher.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/std/expected.hh"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace clockwork::tools::tests::support
{

/// Test message publisher
/// @tparam MessageType Test message type
template <typename MessageType>
  requires TappyType<MessageType>
class TestPublisher
{
private:
  /// Private constructor, use try_open to create an instance
  /// @param[in] publisher_ptr Publisher open result pointer
  explicit TestPublisher(std::shared_ptr<clockwork::pinion::AbstractPublisher> publisher_ptr);

public:
  ~TestPublisher() = default;

  TestPublisher(const TestPublisher&) = delete;
  TestPublisher(TestPublisher&&) noexcept = default;
  TestPublisher& operator=(const TestPublisher&) = delete;
  TestPublisher& operator=(TestPublisher&&) noexcept = default;

  /// Try to create a test publisher
  /// @param[in] pinion_shm_root Shared memory root directory
  /// @param[in] socket_ns Pinion socket namespace
  /// @param[in] uuid_str Channel UUID string
  /// @param[in] channel_Name Channel name
  /// @param[in] num_slots Number of pinion buffer slots
  /// @param[in] message_size_b Pinion message size in bytes
  /// @return Test publisher
  [[nodiscard]] static std::shared_ptr<TestPublisher> open(
    std::string_view pinion_shm_root,
    std::string_view socket_ns,
    std::string_view uuid_str,
    std::string_view channel_name,
    size_t num_slots);

  /// Publish a message on the channel
  /// @param[in] message_time Message timestamp
  /// @param[in] message Test message
  /// @return MonoError on failure
  [[nodiscard]] jewels::expected<void, jewels::MonoError> publish(int64_t message_time, const MessageType& message);

  /// Publish a serialized message on the channel
  /// @param[in] message_time Message timestamp
  /// @param[in] data Message data
  /// @return MonoError on failure
  [[nodiscard]] jewels::expected<void, jewels::MonoError>
  publish(int64_t message_time, std::span<const std::byte> data);

private:
  /// Pinion publisher pointer
  std::shared_ptr<pinion::AbstractPublisher> publisher_ptr_;
};

} // namespace clockwork::tools::tests::support

#include "clockwork/tools/channel_spy/tests/support/test_publisher.inl"
