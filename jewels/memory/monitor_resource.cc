// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/memory/monitor_resource.hh"

#include "jewels/log_cerr/log_cerr.hh"

#include <string>
#include <utility>

namespace jewels::memory
{

MonitorResource::MonitorResource(size_t warn_threshold, std::string_view name)
  : base_(std::pmr::get_default_resource()),
    name_(jewels::container::BoundedString<max_name>::truncated(name)),
    threshold_(warn_threshold)
{
}

size_t MonitorResource::used() const noexcept
{
  return used_;
}

size_t MonitorResource::peak() const noexcept
{
  return peak_;
}

size_t MonitorResource::threshold() const noexcept
{
  return threshold_;
}

void* MonitorResource::do_allocate(std::size_t bytes, std::size_t alignment)
{
  constexpr auto relaxed = std::memory_order_relaxed;
  auto used = (bytes + used_.fetch_add(bytes, relaxed));
  // Maybe update the peak value
  auto peak = peak_.load(relaxed);
  while (used > peak)
  {
    if (peak_.compare_exchange_weak(peak, used, relaxed, relaxed))
    {
      break;
    }
  }
  // Maybe update the threshold if needed, issuing a warning if updated
  auto threshold = threshold_.load(relaxed);
  auto next_threshold = used + warn_delta;
  while (threshold > 0 && used > threshold)
  {
    if (threshold_.compare_exchange_weak(threshold, next_threshold, relaxed, relaxed))
    {
      jewels::log_cerr_warn(
        "Memory for '{}' reached a new peak of {} (limit is {})", name_.to_string_view(), used, threshold);
      break;
    }
  }
  return base_->allocate(bytes, alignment);
}

void MonitorResource::do_deallocate(void* ptr, std::size_t bytes, std::size_t alignment)
{
  used_ -= bytes;
  base_->deallocate(ptr, bytes, alignment);
}

bool MonitorResource::do_is_equal(const memory_resource& other) const noexcept
{
  return this == std::addressof(other);
}

} // namespace jewels::memory
