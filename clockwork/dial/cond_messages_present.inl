// IWYU pragma: private, include "clockwork/dial/cond_messages_present.hh"
#pragma once

#include "clockwork/dial/cond_messages_present.hh"

#include "clockwork/dial/exec_condition.hh"

#include <cstdint>

namespace clockwork
{

template <uint32_t bounds_min, uint32_t bounds_max>
constexpr MessagePresentCondition<bounds_min, bounds_max>::MessagePresentCondition(
  const bool is_active, const uint32_t num_messages) noexcept
  : ExecCondition{is_active}, num_messages_(num_messages)
{
}

template <uint32_t bounds_min, uint32_t bounds_max>
[[nodiscard]] constexpr uint32_t MessagePresentCondition<bounds_min, bounds_max>::get_num_messages() const noexcept
{
  return num_messages_;
}

template <uint32_t bounds_min, uint32_t bounds_max>
[[nodiscard]] consteval uint32_t MessagePresentCondition<bounds_min, bounds_max>::get_bounds_min() const noexcept
{
  return bounds_min;
}

template <uint32_t bounds_min, uint32_t bounds_max>
[[nodiscard]] consteval uint32_t MessagePresentCondition<bounds_min, bounds_max>::get_bounds_max() const noexcept
{
  return bounds_max;
}

// We can write static unit tests, so they are just here instead of a separate compiled test
static_assert(!MessagePresentCondition<0, 0>{}.is_active());
static_assert(MessagePresentCondition<0, 0>{}.get_num_messages() == 0);
static_assert(MessagePresentCondition<1, 2>{}.get_bounds_min() == 1);
static_assert(MessagePresentCondition<1, 2>{}.get_bounds_max() == 2);
static_assert(MessagePresentCondition<0, 0>{true, {}}.is_active());
static_assert(MessagePresentCondition<0, 0>{true, 2}.get_num_messages() == 2);

} // namespace clockwork
