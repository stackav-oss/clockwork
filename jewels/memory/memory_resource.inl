// IWYU pragma: private, include "jewels/memory/memory_resource.hh"

#pragma once

#include "jewels/memory/memory_resource.hh"

#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/memory/instrumented_pmr_resource.hh"

#include <memory_resource>

namespace jewels::memory
{
constexpr MemoryResource::MemoryResource(std::pmr::memory_resource* memory_resource_ptr) noexcept
  : memory_resource_ptr_(memory_resource_ptr)
{
}

constexpr MemoryResource::MemoryResource(std::pmr::memory_resource& memory_resource_ref) noexcept
  : memory_resource_ptr_(&memory_resource_ref)
{
}

constexpr MemoryResource::operator std::pmr::memory_resource*() noexcept
{
  return memory_resource_ptr_.get();
}

constexpr MemoryResource::operator const std::pmr::memory_resource*() const noexcept
{
  return memory_resource_ptr_.get();
}

template <typename T>
constexpr MemoryResource::operator std::pmr::polymorphic_allocator<T>() noexcept
{
  return memory_resource_ptr_.get();
}

template <typename T>
constexpr MemoryResource::operator std::pmr::polymorphic_allocator<T>() const noexcept
{
  return memory_resource_ptr_.get();
}

inline bool operator==(const MemoryResource& lhs, const MemoryResource& rhs) noexcept
{
  return *static_cast<const std::pmr::memory_resource*>(lhs) == *static_cast<const std::pmr::memory_resource*>(rhs);
}

inline jewels::BinaryOutcome
MemoryResource::get_memory_resource_metrics(jewels::Out<MemoryResourceMetrics> metrics) const noexcept
{
  if (const auto* instrumented_resource = dynamic_cast<InstrumentedPmrResource*>(memory_resource_ptr_.get()))
  {
    instrumented_resource->get_memory_resource_metrics(jewels::Out{*metrics});
    return jewels::success;
  }
  return jewels::failure;
}

inline void MemoryResource::reset_incremental_metrics() noexcept
{
  if (auto* instrumented_resource = dynamic_cast<InstrumentedPmrResource*>(memory_resource_ptr_.get()))
  {
    instrumented_resource->reset_incremental_metrics();
  }
}

} // namespace jewels::memory
