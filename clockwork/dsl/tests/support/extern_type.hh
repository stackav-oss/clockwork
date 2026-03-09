// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

namespace clockwork::external
{

/// Types to test the cpp extern feature in clk.

/// Forward declaration
class ExternalStrongType;

/// Factory function
[[nodiscard]] inline ExternalStrongType make(uint32_t value);

/// Strong type
class ExternalStrongType
{
public:
  ExternalStrongType() = default;
  [[nodiscard]] inline uint32_t get() const;

  [[nodiscard]] bool operator==(const ExternalStrongType& other) const = default;

private:
  friend ExternalStrongType make(uint32_t value);
  /// Keep constructor private to force use of the factory function.
  explicit inline ExternalStrongType(uint32_t value);
  /// Underlying value.
  uint32_t value_{};
};

} // namespace clockwork::external

#include "clockwork/dsl/tests/support/extern_type.inl"
