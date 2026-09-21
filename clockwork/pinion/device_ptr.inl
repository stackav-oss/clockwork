// IWYU pragma: private, include "clockwork/pinion/device_ptr.hh"
#pragma once

#include "clockwork/pinion/device_ptr.hh"

#include <cstddef>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace clockwork::pinion
{

template <typename T>
DevicePtr<T>::DevicePtr()
  : size_(0), cpu_ptr_(nullptr), ptr_(nullptr), impl_(nullptr)
{
}

template <typename T>
DevicePtr<T>::~DevicePtr()
{
  if (impl_ != nullptr)
  {
    impl_->release_device_ptr(size_, cpu_ptr_, ptr_);
  }
}

template <typename T>
DevicePtr<T>::DevicePtr(size_t size, T* cpu_ptr, T* dev_ptr, DevicePtrFactory* impl)
  : size_(size), cpu_ptr_(cpu_ptr), ptr_(dev_ptr), impl_(impl)
{
}

template <typename T>
template <typename U, std::enable_if_t<std::is_convertible_v<U*, T*>, int>>
// NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved) the rvalue is used destructively
DevicePtr<T>::DevicePtr(DevicePtr<U>&& other)
  : size_(other.size_), cpu_ptr_(other.cpu_ptr_), ptr_(other.ptr_), impl_(other.impl_)
{
  other.cpu_ptr_ = nullptr;
  other.ptr_ = nullptr;
  other.impl_ = nullptr;
}

template <typename T>
template <typename U, std::enable_if_t<std::is_convertible_v<U*, T*>, int>>
DevicePtr<T>& DevicePtr<T>::operator=(DevicePtr<U>&& other)
{
  DevicePtr<T> tmp(std::move(other));
  std::swap(size_, tmp.size_);
  std::swap(cpu_ptr_, tmp.cpu_ptr_);
  std::swap(ptr_, tmp.ptr_);
  std::swap(impl_, tmp.impl_);
  return *this;
}

template <typename T>
T* DevicePtr<T>::get() const noexcept
{
  return ptr_;
}

template <typename T>
T* DevicePtr<T>::operator->() const noexcept
{
  return ptr_;
}

template <typename T>
DevicePtr<T>::operator bool() const noexcept
{
  return ptr_ != nullptr;
}

template <typename U, typename V>
inline DevicePtr<U> reinterpret_pointer_cast(DevicePtr<V> ptr)
{
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) this just facilitates reinterpret_cast for callers
  DevicePtr<U> result(ptr.size_, reinterpret_cast<U*>(ptr.cpu_ptr_), reinterpret_cast<U*>(ptr.ptr_), ptr.impl_);
  ptr.cpu_ptr_ = nullptr;
  ptr.ptr_ = nullptr;
  ptr.impl_ = nullptr;
  return result;
}

} // namespace clockwork::pinion
