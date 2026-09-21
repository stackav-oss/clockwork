// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/memory/instrumented_pmr_resource.hh"

#include <atomic>
#include <cstddef>
#include <string_view>

namespace jewels::memory
{

/// Memory resource that instruments the standard library's new_delete_resource for heap allocation.
class NewDeleteMemoryResource : public InstrumentedPmrResource
{
public:
  /// Create a new resource with a name and max number of bytes that can be allocated.
  /// If max_size is set to 0, it implies no size limit.
  /// @param max_size Maximum number of bytes that can be allocated through this resource.
  /// @param name Name of the memory resource.
  NewDeleteMemoryResource(size_t max_size, std::string_view name);

  ~NewDeleteMemoryResource() override = default;
  NewDeleteMemoryResource(const NewDeleteMemoryResource&) = delete;
  NewDeleteMemoryResource& operator=(const NewDeleteMemoryResource&) = delete;
  NewDeleteMemoryResource(NewDeleteMemoryResource&&) = delete;
  NewDeleteMemoryResource& operator=(NewDeleteMemoryResource&&) = delete;

  /// Get the memory resource's allocation counters
  /// @param metrics Metrics object to fill.
  /// @return OK if metrics were updated successfully.
  void get_memory_resource_metrics(jewels::Out<MemoryResourceMetrics> metrics) const noexcept override;

  /// Reset the running allocation and deallocation counters.
  void reset_incremental_metrics() noexcept override;

private:
  std::atomic<size_t> used_ = 0;
  std::atomic<size_t> peak_ = 0;
  std::atomic<size_t> alloc_ = 0;
  std::atomic<size_t> dealloc_ = 0;

  /// Called to allocate memory.
  [[nodiscard]] void* do_allocate(std::size_t bytes, std::size_t alignment) override;

  /// Called to free memory.
  void do_deallocate(void* ptr, std::size_t bytes, std::size_t alignment) override;

  /// Checks if this is identical to other because even if `base_==other.base_` mixing allocations and deallocations
  /// would mess up the counters.
  [[nodiscard]] bool do_is_equal(const memory_resource& other) const noexcept override;
};
} // namespace jewels::memory
