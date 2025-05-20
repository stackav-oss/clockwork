// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/container/bounded_string.hh"

#include <atomic>
#include <cstddef>
#include <memory_resource>
#include <string_view>

namespace jewels::memory
{

///
/// A pmr::memory_resource implementation that wraps the default_resource and tracks the number of bytes currently
/// allocated through it.
/// See https://en.cppreference.com/w/cpp/memory/memory_resource
///
class MonitorResource : public std::pmr::memory_resource
{
public:
  static constexpr size_t warn_delta = 1024ULL * 1024ULL;
  static constexpr size_t max_name = 100;

  ///
  /// Constructs the monitor resource using the default_resource for the backing memory
  ///
  explicit MonitorResource(size_t warn_threshold = 0, std::string_view name = "unknown");

  ///
  /// Returns the number of bytes currently allocated through this resource
  ///
  [[nodiscard]] size_t used() const noexcept;

  ///
  /// Returns the number of bytes currently allocated through this resource
  ///
  [[nodiscard]] size_t peak() const noexcept;

  ///
  /// Returns the number of bytes at which the next over-limit warning will occur.  This value increases by `warn_delta`
  /// whenever a warning is issued so will roughly track 'peak_ + warn_delta', once the initial threshold is exceeded
  ///
  [[nodiscard]] size_t threshold() const noexcept;

private:
  std::pmr::memory_resource* base_;
  jewels::container::BoundedString<max_name> name_;
  std::atomic<size_t> used_ = 0;
  std::atomic<size_t> peak_ = 0;
  std::atomic<size_t> threshold_ = 0;

  ///
  /// Called to allocate memory.  Adds `bytes` to `used_` and returns `base_->do_allocate()`
  ///
  [[nodiscard]] void* do_allocate(std::size_t bytes, std::size_t alignment) override;

  ///
  /// Called to free memory.  Subtracts `bytes` to `used_` and returns `base_->do_deallocate()`
  ///
  void do_deallocate(void* ptr, std::size_t bytes, std::size_t alignment) override;

  ///
  /// Checks if this is identical to other because even if `base_==other.base_` mixing allocations and deallocations
  /// would mess up the `used_` count
  ///
  [[nodiscard]] bool do_is_equal(const memory_resource& other) const noexcept override;
};

} // namespace jewels::memory
