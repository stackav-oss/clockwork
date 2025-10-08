// IWYU pragma: private, include "jewels/std/span.hh"
#pragma once

#include <span>
#include <type_traits>

namespace jewels
{

template <class Value>
// NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward) Forward not needed for span constructor.
std::span<std::remove_reference_t<Value>, 1U> as_single_item_span(Value&& value) noexcept
{
  return std::span<std::remove_reference_t<Value>, 1U>{&value, 1U};
}

} // namespace jewels
