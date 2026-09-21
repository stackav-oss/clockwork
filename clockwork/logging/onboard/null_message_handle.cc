// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/onboard/null_message_handle.hh"

namespace clockwork_logging::onboard
{

// NOLINTNEXTLINE(readability-convert-member-functions-to-static) Not all handles have static methods
[[nodiscard]] bool NullMessageHandle::is_valid() const noexcept
{
  return true;
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static) Not all handles have static methods
[[nodiscard]] bool NullMessageHandle::supports_zero_copy() const noexcept
{
  return false;
}

} // namespace clockwork_logging::onboard
