// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/memory/memory_resource.hh"

#include <memory>
#include <memory_resource>
#include <optional>

namespace jewels::memory
{
namespace detail
{
template <typename T>
class PmrDeleter
{
  using AllocType = std::pmr::polymorphic_allocator<T>;
  using AllocTraits = std::allocator_traits<AllocType>;

public:
  PmrDeleter() = default;
  explicit PmrDeleter(jewels::memory::MemoryResource memres);

  void operator()(typename AllocTraits::pointer object);

private:
  std::optional<jewels::memory::MemoryResource> memres_;
};
} // namespace detail

///
/// Alias for a unique_ptr using a pmr-aware deleter.
/// Construct using `make_pmr_unique()`
///
template <typename T>
using pmr_unique_ptr = std::unique_ptr<T, detail::PmrDeleter<T>>;

///
/// Creates an object much like `std::make_unique` but uses the provided memory resource for the heap storage.
/// @param memres The memory resource that should be used for allocation and (later) deallocation
/// @param args A pack of args used to construct the new element.
///
template <typename T, typename... Args>
auto make_pmr_unique(jewels::memory::MemoryResource memres, Args&&... args);

} // namespace jewels::memory

#include "jewels/memory/pmr_unique_ptr.inl"
