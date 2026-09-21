// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/callsig/outparam.hh"
#include "jewels/container/bounded_string.hh"

#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <string_view>

namespace jewels::memory
{

/// Allocation metrics for memory resources.
struct MemoryResourceMetrics
{
  size_t total_allocated = 0;   /// Total bytes allocated since the memory resource was created
  size_t total_deallocated = 0; /// Total bytes deallocated since the memory resource was created
  size_t current_allocated = 0; /// Number of bytes currently allocated through a resource
  size_t peak_allocated = 0;    /// Maximum number of bytes allocated since the memory resource was created
};

/// Extends std::pmr::memory_resource with a name and allocation counters.
class InstrumentedPmrResource : public std::pmr::memory_resource
{
public:
  static constexpr size_t max_name = 512;
  InstrumentedPmrResource(size_t max_size, std::string_view name);
  ~InstrumentedPmrResource() override = default;
  InstrumentedPmrResource(const InstrumentedPmrResource&) = default;
  InstrumentedPmrResource& operator=(const InstrumentedPmrResource&) = default;
  InstrumentedPmrResource(InstrumentedPmrResource&&) = default;
  InstrumentedPmrResource& operator=(InstrumentedPmrResource&&) = default;

  [[nodiscard]] std::string_view get_name() const noexcept;
  [[nodiscard]] uint64_t get_max_size() const noexcept;

  /// Get the memory resource's allocation counters.
  /// @param metrics Metrics object to fill.
  virtual void get_memory_resource_metrics(jewels::Out<MemoryResourceMetrics> metrics) const noexcept = 0;

  /// Reset any incremental memory resource metrics.
  virtual void reset_incremental_metrics() noexcept = 0;

private:
  jewels::container::BoundedString<max_name> name_;
  size_t max_size_;
};
} // namespace jewels::memory
