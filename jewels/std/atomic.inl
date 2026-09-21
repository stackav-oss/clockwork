// IWYU pragma: private, include "jewels/std/atomic.hh"
#pragma once
#include "jewels/std/atomic.hh"

#include <atomic>

namespace jewels
{
template <class T>
T atomic_fetch_max_explicit(
  std::atomic<T>* obj, typename std::atomic<T>::value_type arg, std::memory_order order) noexcept
{
  T expected = obj->load(std::memory_order_relaxed);
  while (expected < arg && !obj->compare_exchange_weak(expected, arg, order, order))
  {
  }
  return expected;
}
} // namespace jewels
