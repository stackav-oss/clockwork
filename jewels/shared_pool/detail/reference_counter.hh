// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <cstdint>

namespace jewels::detail
{

/// Non-thread safe reference counter
class ReferenceCounter
{
public:
  /// Constructor
  ReferenceCounter() noexcept = default;
  ~ReferenceCounter() noexcept = default;

  ReferenceCounter(const ReferenceCounter&) = delete;
  ReferenceCounter& operator=(const ReferenceCounter&) = delete;
  ReferenceCounter(ReferenceCounter&&) = delete;
  ReferenceCounter& operator=(ReferenceCounter&&) = delete;

  /// Set the reference count
  /// @param[in] count Value to set
  inline void set(uint32_t value) noexcept;

  /// Get the reference count
  /// @return Reference count
  [[nodiscard]] inline uint32_t get() const noexcept;

  /// Increment the reference count
  inline void increment() noexcept;

  /// Decrement the reference count
  /// @return True if the reference count decremented to zero
  [[nodiscard]] inline bool decrement() noexcept;

private:
  /// Reference count
  uint32_t ref_count_{0U};
};

/// Atomic reference counter
class AtomicReferenceCounter
{
public:
  /// Constructor
  AtomicReferenceCounter() noexcept = default;
  ~AtomicReferenceCounter() noexcept = default;

  AtomicReferenceCounter(const AtomicReferenceCounter&) = delete;
  AtomicReferenceCounter& operator=(const AtomicReferenceCounter&) = delete;
  AtomicReferenceCounter(AtomicReferenceCounter&&) = delete;
  AtomicReferenceCounter& operator=(AtomicReferenceCounter&&) = delete;

  /// Set the reference count
  /// @param[in] count Value to set
  inline void set(uint32_t value) noexcept;

  /// Get the reference count
  /// @return Reference count
  [[nodiscard]] inline uint32_t get() const noexcept;

  /// Increment the reference count
  inline void increment() noexcept;

  /// Decrement the reference count
  /// @return True if the reference count decremented to zero
  [[nodiscard]] inline bool decrement() noexcept;

private:
  /// Reference count
  std::atomic<uint32_t> ref_count_{0U};
};

} // namespace jewels::detail

#include "jewels/shared_pool/detail/reference_counter.inl"
