// IWYU pragma: private, include "clockwork/dial/cond_dynamic_timer.hh"
#pragma once

#include "clockwork/dial/cond_dynamic_timer.hh"

#include "clockwork/dial/exec_condition.hh"

namespace clockwork
{

constexpr DynamicTimerCondition::DynamicTimerCondition(bool is_active) noexcept
  : ExecCondition(is_active)
{
}

} // namespace clockwork
