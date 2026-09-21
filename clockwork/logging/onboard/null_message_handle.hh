// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace clockwork_logging::onboard
{

/// Null message handle for messages that cannot be zero copied
class NullMessageHandle
{
public:
  /// Constructor
  NullMessageHandle() noexcept = default;
  ~NullMessageHandle() = default;

  NullMessageHandle(const NullMessageHandle&) = default;
  NullMessageHandle& operator=(const NullMessageHandle&) = default;
  NullMessageHandle(NullMessageHandle&&) = default;
  NullMessageHandle& operator=(NullMessageHandle&&) = default;

  /// Test whether the message buffer is valid
  /// @return True, null message buffers are always valid
  [[nodiscard]] bool is_valid() const noexcept;

  /// Test whether the message can be zero copied
  /// @return False, null messages cannot be zero copied
  [[nodiscard]] bool supports_zero_copy() const noexcept;
};

} // namespace clockwork_logging::onboard
