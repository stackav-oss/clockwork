// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/log_cerr/log_level.hh"

namespace jewels
{
class DetectLogThreshold
{
public:
  /// Constructor
  /// @note Not thread safe.
  DetectLogThreshold() noexcept;

  [[nodiscard]] LogLevel value() const
  {
    return log_threshold_;
  };

private:
  LogLevel log_threshold_{LogLevel::info};
};
} // namespace jewels
