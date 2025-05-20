// IWYU pragma: private, include "clockwork/logging/wrapping_counter.hh"
#pragma once

#include "clockwork/logging/wrapping_counter.hh"

#include "jewels/meta/concepts.hh"

namespace clockwork_logging
{

template <jewels::meta::Integral Counter>
constexpr WrappingCounter<Counter>::WrappingCounter(Counter value) noexcept
  : value_(value)
{
}

template <jewels::meta::Integral Counter>
constexpr Counter WrappingCounter<Counter>::value() const noexcept
{
  return value_;
}

} // namespace clockwork_logging
