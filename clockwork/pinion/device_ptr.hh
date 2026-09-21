// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>
#include <memory_resource>
#include <string>
#include <string_view>

namespace clockwork::pinion
{
class DevicePtrFactory;

///
/// Wrapper for a reference to an object in device memory
///
template <typename T>
class DevicePtr // NOLINT(cppcoreguidelines-special-member-functions) template move not being detected
{
public:
  /// Creates a null object
  DevicePtr();
  ~DevicePtr();

  /// Creates a wrapper
  DevicePtr(size_t size, T* cpu_ptr, T* dev_ptr, DevicePtrFactory* impl);

  // Disallow copies since otherwise this would need refcounting
  DevicePtr(const DevicePtr&) = delete;
  DevicePtr& operator=(const DevicePtr&) = delete;

  /// implicit const casts
  template <typename U, std::enable_if_t<std::is_convertible_v<U*, T*>, int> = 0>
  DevicePtr(DevicePtr<U>&& other); // NOLINT(google-explicit-constructor) basically the normal move cstr
  template <typename U, std::enable_if_t<std::is_convertible_v<U*, T*>, int> = 0>
  DevicePtr& operator=(DevicePtr<U>&& other);

  /// Returns a pointer to the object in device memory.
  T* get() const noexcept;
  T* operator->() const noexcept;

  /// Checks if the underlying pointer is null.
  explicit operator bool() const noexcept;

private:
  template <typename U>
  friend class DevicePtr;

  template <typename U, typename V>
  friend inline DevicePtr<U> reinterpret_pointer_cast(DevicePtr<V> ptr);

  size_t size_;
  T* cpu_ptr_;
  T* ptr_;
  DevicePtrFactory* impl_;
};

///
/// Interface for a Slot to bind / release a device pointer on demand
///
class DevicePtrFactory
{
public:
  DevicePtrFactory() = default;
  virtual ~DevicePtrFactory() = default;
  DevicePtrFactory(const DevicePtrFactory&) = default;
  DevicePtrFactory(DevicePtrFactory&&) = default;
  DevicePtrFactory& operator=(const DevicePtrFactory&) = default;
  DevicePtrFactory& operator=(DevicePtrFactory&&) = default;

  /// Create a device pointer, potentially flushing CPU cache.
  /// Returns nullptr on error
  virtual DevicePtr<void> make_device_ptr(size_t size, void* cpu_ptr) = 0;
  virtual DevicePtr<const void> make_device_ptr(size_t size, const void* cpu_ptr) = 0;

  /// Releases the device pointer, potentially flushing GPU cache
  virtual void release_device_ptr(size_t size, void* cpu_ptr, void* dev_ptr) = 0;
  virtual void release_device_ptr(size_t size, const void* cpu_ptr, const void* dev_ptr) = 0;
};

} // namespace clockwork::pinion

#include "clockwork/pinion/device_ptr.inl"
