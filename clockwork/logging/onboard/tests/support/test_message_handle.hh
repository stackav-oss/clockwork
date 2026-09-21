// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace clockwork_logging::onboard
{

/// Message handle for unit testing
/// @tparam BufferType Message buffer type
template <typename BufferType>
class TestMessageHandle
{
public:
  /// Constructor
  /// @param[in] buffer Message buffer
  explicit TestMessageHandle(BufferType buffer);

  ~TestMessageHandle() = default;

  TestMessageHandle(const TestMessageHandle&) = default;
  TestMessageHandle& operator=(const TestMessageHandle&) = default;
  TestMessageHandle(TestMessageHandle&&) = default;
  TestMessageHandle& operator=(TestMessageHandle&&) = default;

  /// Test whether the message buffer is valid
  /// @return True if the message is valid
  [[nodiscard]] bool is_valid() const noexcept;

  /// Set the message buffer validity
  /// @param[in] is_valid Message buffer validity flag
  void set_is_valid(bool is_valid) noexcept;

  /// Test whether the handle supports zero copy
  /// @return True, test message handles support zero copy
  [[nodiscard]] bool supports_zero_copy() const noexcept;

private:
  /// Message buffer
  BufferType buffer_;

  /// Validity flag
  bool is_valid_{true};
};

} // namespace clockwork_logging::onboard

#include "clockwork/logging/onboard/tests/support/test_message_handle.inl"
