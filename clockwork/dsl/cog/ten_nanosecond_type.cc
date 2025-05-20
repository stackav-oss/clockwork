// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/dsl/cog/ten_nanosecond_type.hh"

namespace clockwork
{

TenNanoseconds ten_nanoseconds_factory(uint32_t value)
{
  return TenNanoseconds(value);
}
} // namespace clockwork
