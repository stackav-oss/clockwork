// IWYU pragma: private, include "clockwork/logging/onboard/tests/support/test_message_handle.hh"
#pragma once

#include "clockwork/logging/onboard/tests/support/test_message_handle.hh"

#include <utility>

namespace clockwork_logging::onboard
{

template <typename BufferType>
TestMessageHandle<BufferType>::TestMessageHandle(BufferType buffer)
  : buffer_(std::move(buffer))
{
}

template <typename BufferType>
[[nodiscard]] bool TestMessageHandle<BufferType>::is_valid() const noexcept
{
  return is_valid_;
}

template <typename BufferType>
void TestMessageHandle<BufferType>::set_is_valid(bool is_valid) noexcept
{
  is_valid_ = is_valid;
}

template <typename BufferType>
// NOLINTNEXTLINE(readability-convert-member-functions-to-static) needs to conform to MessageHandle
[[nodiscard]] bool TestMessageHandle<BufferType>::supports_zero_copy() const noexcept
{
  return true;
}

} // namespace clockwork_logging::onboard
