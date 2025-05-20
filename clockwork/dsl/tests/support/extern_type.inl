// IWYU pragma: private, include "clockwork/dsl/tests/support/extern_type.hh"
#pragma once

#include "clockwork/dsl/tests/support/extern_type.hh"

#include <cstdint>

namespace clockwork::external
{

ExternalStrongType make(uint32_t value)
{
  return ExternalStrongType{value};
}

ExternalStrongType::ExternalStrongType(uint32_t value)
  : value_{value}
{
}

uint32_t ExternalStrongType::get() const
{
  return value_;
}

} // namespace clockwork::external
