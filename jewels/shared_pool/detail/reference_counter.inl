// IWYU pragma: private, include "jewels/shared_pool/detail/reference_counter.hh"
#pragma once

#include "jewels/shared_pool/detail/reference_counter.hh"

#include <atomic>
#include <cstdint>

namespace jewels::detail
{

void ReferenceCounter::set(uint32_t value) noexcept
{
  ref_count_ = value;
}

[[nodiscard]] uint32_t ReferenceCounter::get() const noexcept
{
  return ref_count_;
}

void ReferenceCounter::increment() noexcept
{
  ++ref_count_;
}

[[nodiscard]] bool ReferenceCounter::decrement() noexcept
{
  --ref_count_;
  return (ref_count_ == 0U);
}

void AtomicReferenceCounter::set(uint32_t value) noexcept
{
  ref_count_.store(value, std::memory_order_relaxed);
}

[[nodiscard]] uint32_t AtomicReferenceCounter::get() const noexcept
{
  return ref_count_.load(std::memory_order_relaxed);
}

void AtomicReferenceCounter::increment() noexcept
{
  ref_count_.fetch_add(1U, std::memory_order_relaxed);
}

[[nodiscard]] bool AtomicReferenceCounter::decrement() noexcept
{
  const auto prev_count = ref_count_.fetch_sub(1U, std::memory_order_relaxed);
  return (prev_count == 1U);
}

} // namespace jewels::detail
