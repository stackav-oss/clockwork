// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/repr_iface.hh"
#include "clockwork/tools/channel_spy/channel_spy_config.hh"
#include "clockwork/tools/channel_spy/tests/support/test_publisher.hh"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

namespace clockwork::tools::tests::support
{

/// Helper class for python testing
template <typename MessageType>
  requires TappyType<MessageType>
class TestHelper
{
private:
  /// Private constructor, use make_test_helper to create an instance
  /// @param[in] pinion_shm_root Pinion shared memory root directory
  /// @param[in] socket_ns Pinion socket namespace
  /// @param[in] config Channel spy configuration
  TestHelper(
    std::string_view pinion_shm_root, std::string_view socket_ns, std::unique_ptr<ChannelSpyConfigTap> spy_config);

public:
  ~TestHelper() = default;

  TestHelper(const TestHelper&) = delete;
  TestHelper(TestHelper&&) noexcept = default;
  TestHelper& operator=(const TestHelper&) = delete;
  TestHelper& operator=(TestHelper&&) noexcept = default;

  /// Make a test helper
  /// @param[in] pinion_shm_root Pinion shared memory root directory
  /// @param[in] socket_ns Pinion socket namespace
  /// @return Test helper instance
  /// @throws runtime_error on failure
  [[nodiscard]] static std::shared_ptr<TestHelper>
  make_test_helper(std::string_view pinion_shm_root, std::string_view socket_ns);

  /// Channel spy onfiguration accessor
  /// @return Channel spy config
  [[nodiscard]] const ChannelSpyConfigTap& spy_config() const;

  /// Channel name accessor
  /// @param[in] channel_index Channel configuration index
  /// @return Channel name
  /// @throws runtime_error on failure
  [[nodiscard]] std::string_view channel_name(size_t channel_index) const;

  /// Create a test publisher for the channel at the specified index
  /// @param[in] channel_index Channel configuration index
  /// @return Channel test publisher
  /// @throws runtime_error on failure
  [[nodiscard]] std::shared_ptr<TestPublisher<MessageType>> open_publisher(size_t channel_index) const;

private:
  /// Pinion shared memory root directory
  std::string pinion_shm_root_;

  /// Pinion socket namespace
  std::string socket_ns_;

  /// Channel spy configuration
  std::unique_ptr<ChannelSpyConfigTap> spy_config_;
};

} // namespace clockwork::tools::tests::support

#include "clockwork/tools/channel_spy/tests/support/test_helper.inl"
