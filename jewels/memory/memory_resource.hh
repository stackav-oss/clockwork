// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/memory/instrumented_pmr_resource.hh"
#include "jewels/memory/pointers.hh"

#include <memory_resource>
#include <unordered_map>

namespace jewels::memory
{

/// A non-nullable wrapper around `std::pmr::memory_resource`.
class MemoryResource
{
public:
  /// Construct a `MemoryResource` from a `std::pmr::memory_resource*`.
  /// @param[in] memory_resource_ptr A standard library memory_resource.
  /// @pre `memory_resource_ptr` must not be `nullptr`.
  constexpr explicit MemoryResource(std::pmr::memory_resource* memory_resource_ptr) noexcept;

  /// Construct a `MemoryResource` from a `std::pmr::memory_resource&`.
  /// @param[in] memory_resource_ref A standard library memory_resource.
  constexpr explicit MemoryResource(std::pmr::memory_resource& memory_resource_ref) noexcept;

  /// MemoryResource cannot be default constructed because it needs an underlying `std::pmr::memory_resource` to use.
  MemoryResource() noexcept = delete;

  ~MemoryResource() noexcept = default;
  constexpr MemoryResource(MemoryResource&&) noexcept = default;
  constexpr MemoryResource(const MemoryResource&) noexcept = default;
  constexpr MemoryResource& operator=(MemoryResource&&) noexcept = default;
  constexpr MemoryResource& operator=(const MemoryResource&) noexcept = default;

  /// Explicit conversion to the standard library memory_resource type.
  constexpr explicit operator std::pmr::memory_resource*() noexcept;
  /// Explicit conversion to the standard library memory_resource type.
  constexpr explicit operator const std::pmr::memory_resource*() const noexcept;

  /// For convenience we provide an implicit conversion to `std::pmr::polymorphic_allocator` so that `MemoryResource`
  /// can be passed directly to constructors like `std::pmr::string`.
  /// @tparam T The type the new `polymorphic_allocator` allocates.
  template <typename T>
  // NOLINTNEXTLINE(google-explicit-constructor) - See docblock for why this is implicit.
  constexpr operator std::pmr::polymorphic_allocator<T>() noexcept;

  /// For convenience we provide an implicit conversion to `std::pmr::polymorphic_allocator` so that `MemoryResource`
  /// can be passed directly to constructors like `std::pmr::string`.
  /// @tparam T The type the new `polymorphic_allocator` allocates.
  template <typename T>
  // NOLINTNEXTLINE(google-explicit-constructor) - See docblock for why this is implicit.
  constexpr operator std::pmr::polymorphic_allocator<T>() const noexcept;

  /// Get metrics if the underlying memory_resource supports them.
  /// @param metrics Metrics object to fill.
  /// @return success if metrics were retrieved, failure otherwise.
  inline jewels::BinaryOutcome get_memory_resource_metrics(jewels::Out<MemoryResourceMetrics> metrics) const noexcept;

  /// Reset incremental metrics if the underlying memory_resource support metrics.
  inline void reset_incremental_metrics() noexcept;

private:
  // The underlying memory_resource pointer.
  ObjectPtr<std::pmr::memory_resource> memory_resource_ptr_;
};

} // namespace jewels::memory

#include "jewels/memory/memory_resource.inl"
