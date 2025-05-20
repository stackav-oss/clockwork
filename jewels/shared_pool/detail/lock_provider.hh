// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <mutex>

namespace jewels::detail
{

/// Non-thread safe reference counter
class NullLockProvider
{
public:
  NullLockProvider() noexcept = default;
  ~NullLockProvider() noexcept = default;

  NullLockProvider(const NullLockProvider&) = delete;
  NullLockProvider& operator=(const NullLockProvider&) = delete;
  NullLockProvider(NullLockProvider&&) = delete;
  NullLockProvider& operator=(NullLockProvider&&) = delete;

  /// No-op lock method
  /// @return Dummy lock guard value
  [[nodiscard]] static inline char lock();
};

/// Mutex lock provider
class MutexLockProvider
{
public:
  MutexLockProvider() noexcept = default;
  ~MutexLockProvider() noexcept = default;

  MutexLockProvider(const MutexLockProvider&) = delete;
  MutexLockProvider& operator=(const MutexLockProvider&) = delete;
  MutexLockProvider(MutexLockProvider&&) = delete;
  MutexLockProvider& operator=(MutexLockProvider&&) = delete;

  /// Lock method
  /// @return Lock guard for the mutex
  [[nodiscard]] inline std::lock_guard<std::mutex> lock();

private:
  /// Mutex
  std::mutex mutex_;
};

} // namespace jewels::detail

#include "jewels/shared_pool/detail/lock_provider.inl"
