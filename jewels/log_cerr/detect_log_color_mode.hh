// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace jewels
{
class DetectLogColorMode
{
public:
  /// Constructor
  /// @note Not thread safe.
  DetectLogColorMode() noexcept;

  [[nodiscard]] bool should_use_color() const
  {
    return use_color_;
  };

private:
  bool use_color_{false};
}; // class DetectLogColorMode
} // namespace jewels
