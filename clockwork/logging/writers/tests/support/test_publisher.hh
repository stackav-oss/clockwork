// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/pinion/shm_publisher.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace clockwork_logging::tests
{

/// Test message publisher
class TestPublisher
{
private:
  /// Private constructor, use try_open to create an instance
  /// @param[in] channel_name Channel name
  /// @param[in] publisher_ptr Publisher open result pointer
  TestPublisher(std::string_view channel_name, std::shared_ptr<clockwork::pinion::ShmPublisher> publisher_ptr);

public:
  ~TestPublisher() = default;

  TestPublisher(const TestPublisher&) = delete;
  TestPublisher(TestPublisher&&) noexcept = default;
  TestPublisher& operator=(const TestPublisher&) = delete;
  TestPublisher& operator=(TestPublisher&&) noexcept = default;

  /// Try to create a test publisher
  /// @param[in] memory_resource Memory resource
  /// @param[in] pinion_shm_root Shared memory root directory
  /// @param[in] pinion_namespace Pinion namespace
  /// @param[in] uuid_str Channel UUID string
  /// @param[in] num_slots Number of pinion buffer slots
  /// @param[in] message_size_b Pinion message size in bytes
  /// @return Test publisher or MonoError on failure
  [[nodiscard]] static jewels::expected<TestPublisher, jewels::MonoError> try_open(
    jewels::memory::MemoryResource memory_resource,
    std::string_view pinion_shm_root,
    std::string_view pinion_namespace,
    std::string_view uuid_str,
    std::string_view channel_name,
    size_t num_slots,
    size_t message_size_b);

  /// Call the underlying publisher's on_connect_pending
  /// @return true if all pending connections could be accepted, false if any were rejected
  [[nodiscard]] bool on_connect_pending();

  /// Call the underlying publisher's num_clients method
  /// @return Number of clients connected
  [[nodiscard]] size_t num_clients() const;

  /// Publish a message on the channel
  /// @param[in] message_time Message timestamp
  /// @param[in] data Message data
  /// @return MonoError on failure
  [[nodiscard]] jewels::expected<void, jewels::MonoError>
  try_publish(LogTimestamp message_time, std::span<const std::byte> data);

  /// Get the channel name
  /// @return channel name
  [[nodiscard]] std::string_view get_channel_name() const noexcept;

  /// Get the underlying publisher
  /// @return underlying publisher
  [[nodiscard]] std::shared_ptr<clockwork::pinion::ShmPublisher> underlying_publisher();

private:
  /// Channel name
  std::string channel_name_;

  /// Publisher open result
  std::shared_ptr<clockwork::pinion::ShmPublisher> publisher_ptr_;
};

} // namespace clockwork_logging::tests
