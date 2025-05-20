// IWYU pragma: private, include "clockwork/dial/exec_condition.hh"
#pragma once

#include "clockwork/dial/exec_condition.hh"
namespace clockwork
{

constexpr ExecCondition::ExecCondition(const bool is_active) noexcept
  : is_active_(is_active)
{
}

constexpr bool ExecCondition::is_active() const noexcept
{
  return is_active_;
}

constexpr ExecCondition::operator bool() const noexcept
{
  return is_active();
}

// We can write static unit tests, so they are just here instead of a separate compiled test:
static_assert(ExecCondition{true}.is_active());
static_assert(ExecCondition{true});
static_assert(!ExecCondition().is_active());
static_assert(!ExecCondition());
static_assert(!ExecCondition{false}.is_active());
static_assert(!ExecCondition{false});

} // namespace clockwork
