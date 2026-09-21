// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/memory/new_delete_memory_resource.hh"

#include "jewels/callsig/outparam.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/std/atomic.hh"

#include <new>

namespace jewels::memory
{
namespace
{
constexpr auto relaxed = std::memory_order_relaxed;
}

NewDeleteMemoryResource::NewDeleteMemoryResource(size_t max_size, std::string_view name)
  : InstrumentedPmrResource(max_size, name)
{
}

void* NewDeleteMemoryResource::do_allocate(std::size_t bytes, std::size_t alignment)
{
  // TODO(OI-4274) Check if the allocation would exceed max_size before performing allocation.
  auto used = (bytes + used_.fetch_add(bytes, relaxed));
  alloc_.fetch_add(bytes, relaxed);
  jewels::atomic_fetch_max_explicit(&peak_, used, relaxed);

  return ::operator new(bytes, std::align_val_t(alignment));
}

void NewDeleteMemoryResource::do_deallocate(void* ptr, std::size_t bytes, std::size_t alignment)
{
  const auto previous = used_.fetch_sub(bytes, relaxed);
  if (previous < bytes)
  {
    used_.fetch_add(bytes - previous, relaxed);
    jewels::log_cerr_error(
      "{}: Deallocation of {} bytes would underflow used memory counter {}.", get_name(), bytes, previous);
  }
  dealloc_.fetch_add(bytes, relaxed);

  ::operator delete(ptr, std::align_val_t(alignment));
}

bool NewDeleteMemoryResource::do_is_equal(const memory_resource& other) const noexcept
{
  return this == &other;
}

void NewDeleteMemoryResource::get_memory_resource_metrics(jewels::Out<MemoryResourceMetrics> metrics) const noexcept
{
  metrics->total_allocated = alloc_;
  metrics->total_deallocated = dealloc_;
  metrics->current_allocated = used_;
  metrics->peak_allocated = peak_;
}

void NewDeleteMemoryResource::reset_incremental_metrics() noexcept
{
  alloc_.store(0, std::memory_order_relaxed);
  dealloc_.store(0, std::memory_order_relaxed);
}
} // namespace jewels::memory
