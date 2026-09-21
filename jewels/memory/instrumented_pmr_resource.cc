// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/memory/instrumented_pmr_resource.hh"

#include <optional>

namespace jewels::memory
{
InstrumentedPmrResource::InstrumentedPmrResource(size_t max_size, std::string_view name)
  : name_(
      jewels::container::BoundedString<max_name>::try_make(name).value_or(
        jewels::container::BoundedString<max_name>{})),
    max_size_(max_size)
{
}

std::string_view InstrumentedPmrResource::get_name() const noexcept
{
  return name_.to_string_view();
}

size_t InstrumentedPmrResource::get_max_size() const noexcept
{
  return max_size_;
}
} // namespace jewels::memory
