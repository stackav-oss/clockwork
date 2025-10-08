// IWYU pragma: private, include "jewels/shared_pool/ref_counted_pool.hh"
#pragma once

#include "jewels/shared_pool/ref_counted_pool.hh"

#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <memory_resource>
#include <regex>
#include <span>

namespace jewels::detail
{

template <
  typename Derived,
  typename ValueType,
  typename ContainerType,
  typename ReferenceCounterType,
  typename LockProviderType>
RefCountedPoolImpl<Derived, ValueType, ContainerType, ReferenceCounterType, LockProviderType>::PoolEntry::PoolEntry(
  memory::ObjectPtr<RefCountedPoolImpl<Derived, ValueType, ContainerType, ReferenceCounterType, LockProviderType>>
    pool_ptr_in,
  uint32_t pool_index_in)
  : pool_ptr(pool_ptr_in), pool_index(pool_index_in)
{
}

template <
  typename Derived,
  typename ValueType,
  typename ContainerType,
  typename ReferenceCounterType,
  typename LockProviderType>
RefCountedPoolImpl<Derived, ValueType, ContainerType, ReferenceCounterType, LockProviderType>::SharedReferenceImpl::
  SharedReferenceImpl(PoolEntry* entry_ptr)
  : entry_ptr_(entry_ptr)
{
}

template <
  typename Derived,
  typename ValueType,
  typename ContainerType,
  typename ReferenceCounterType,
  typename LockProviderType>
RefCountedPoolImpl<Derived, ValueType, ContainerType, ReferenceCounterType, LockProviderType>::SharedReferenceImpl::
  ~SharedReferenceImpl()
{
  reset();
}

template <
  typename Derived,
  typename ValueType,
  typename ContainerType,
  typename ReferenceCounterType,
  typename LockProviderType>
RefCountedPoolImpl<Derived, ValueType, ContainerType, ReferenceCounterType, LockProviderType>::SharedReferenceImpl::
  SharedReferenceImpl(const SharedReferenceImpl& other) noexcept
  : entry_ptr_(other.entry_ptr_)
{
  entry_ptr_->reference_count.increment();
}

template <
  typename Derived,
  typename ValueType,
  typename ContainerType,
  typename ReferenceCounterType,
  typename LockProviderType>
typename RefCountedPoolImpl<Derived, ValueType, ContainerType, ReferenceCounterType, LockProviderType>::
  SharedReferenceImpl&
  RefCountedPoolImpl<Derived, ValueType, ContainerType, ReferenceCounterType, LockProviderType>::SharedReferenceImpl::
  operator=(const SharedReferenceImpl& other) noexcept
{
  if (&other != this)
  {
    reset();
    entry_ptr_ = other.entry_ptr_;
    entry_ptr_->reference_count.increment();
  }
  return *this;
}

template <
  typename Derived,
  typename ValueType,
  typename ContainerType,
  typename ReferenceCounterType,
  typename LockProviderType>
RefCountedPoolImpl<Derived, ValueType, ContainerType, ReferenceCounterType, LockProviderType>::SharedReferenceImpl::
  SharedReferenceImpl(SharedReferenceImpl&& other) noexcept
  : entry_ptr_(other.entry_ptr_)
{
  other.entry_ptr_ = nullptr;
}

template <
  typename Derived,
  typename ValueType,
  typename ContainerType,
  typename ReferenceCounterType,
  typename LockProviderType>
typename RefCountedPoolImpl<Derived, ValueType, ContainerType, ReferenceCounterType, LockProviderType>::
  SharedReferenceImpl&
  RefCountedPoolImpl<Derived, ValueType, ContainerType, ReferenceCounterType, LockProviderType>::SharedReferenceImpl::
  operator=(SharedReferenceImpl&& other) noexcept
{
  if (&other != this)
  {
    reset();
    entry_ptr_ = other.entry_ptr_;
    other.entry_ptr_ = nullptr;
  }
  return *this;
}

template <
  typename Derived,
  typename ValueType,
  typename ContainerType,
  typename ReferenceCounterType,
  typename LockProviderType>
[[nodiscard]] ValueType& RefCountedPoolImpl<Derived, ValueType, ContainerType, ReferenceCounterType, LockProviderType>::
  SharedReferenceImpl::operator*()
{
  return static_cast<typename Derived::SharedReference&>(*this).value();
}

template <
  typename Derived,
  typename ValueType,
  typename ContainerType,
  typename ReferenceCounterType,
  typename LockProviderType>
[[nodiscard]] const ValueType&
RefCountedPoolImpl<Derived, ValueType, ContainerType, ReferenceCounterType, LockProviderType>::SharedReferenceImpl::
operator*() const
{
  return static_cast<const typename Derived::SharedReference&>(*this).value();
}

template <
  typename Derived,
  typename ValueType,
  typename ContainerType,
  typename ReferenceCounterType,
  typename LockProviderType>
[[nodiscard]] ValueType* RefCountedPoolImpl<Derived, ValueType, ContainerType, ReferenceCounterType, LockProviderType>::
  SharedReferenceImpl::operator->()
{
  return &static_cast<typename Derived::SharedReference&>(*this).value();
}

template <
  typename Derived,
  typename ValueType,
  typename ContainerType,
  typename ReferenceCounterType,
  typename LockProviderType>
[[nodiscard]] const ValueType*
RefCountedPoolImpl<Derived, ValueType, ContainerType, ReferenceCounterType, LockProviderType>::SharedReferenceImpl::
operator->() const
{
  return &static_cast<const typename Derived::SharedReference&>(*this).value();
}

template <
  typename Derived,
  typename ValueType,
  typename ContainerType,
  typename ReferenceCounterType,
  typename LockProviderType>
[[nodiscard]] RefCountedPoolImpl<Derived, ValueType, ContainerType, ReferenceCounterType, LockProviderType>::
  SharedReferenceImpl::
  operator bool() const
{
  return entry_ptr_ != nullptr;
}

template <
  typename Derived,
  typename ValueType,
  typename ContainerType,
  typename ReferenceCounterType,
  typename LockProviderType>
[[nodiscard]] bool RefCountedPoolImpl<Derived, ValueType, ContainerType, ReferenceCounterType, LockProviderType>::
  SharedReferenceImpl::is_valid() const
{
  return entry_ptr_ != nullptr;
}

template <
  typename Derived,
  typename ValueType,
  typename ContainerType,
  typename ReferenceCounterType,
  typename LockProviderType>
[[nodiscard]] uint32_t RefCountedPoolImpl<Derived, ValueType, ContainerType, ReferenceCounterType, LockProviderType>::
  SharedReferenceImpl::get_reference_count() const
{
  return entry_ptr_ != nullptr ? entry_ptr_->reference_count.get() : 0U;
}

template <
  typename Derived,
  typename ValueType,
  typename ContainerType,
  typename ReferenceCounterType,
  typename LockProviderType>
void RefCountedPoolImpl<Derived, ValueType, ContainerType, ReferenceCounterType, LockProviderType>::
  SharedReferenceImpl::reset()
{
  if (entry_ptr_ != nullptr)
  {
    if (entry_ptr_->reference_count.decrement())
    {
      static_cast<typename Derived::SharedReference*>(this)->reset_storage();
      [[maybe_unused]] const auto guard = entry_ptr_->pool_ptr->lock_.lock();
      entry_ptr_->pool_ptr->avail_indexes_.push_back(entry_ptr_->pool_index);
    }
    entry_ptr_ = nullptr;
  }
}

template <
  typename Derived,
  typename ValueType,
  typename ContainerType,
  typename ReferenceCounterType,
  typename LockProviderType>
typename RefCountedPoolImpl<Derived, ValueType, ContainerType, ReferenceCounterType, LockProviderType>::PoolEntry*
RefCountedPoolImpl<Derived, ValueType, ContainerType, ReferenceCounterType, LockProviderType>::SharedReferenceImpl::
  release()
{
  auto* released_entry_ptr = entry_ptr_;
  entry_ptr_ = nullptr;
  return released_entry_ptr;
}

template <
  typename Derived,
  typename ValueType,
  typename ContainerType,
  typename ReferenceCounterType,
  typename LockProviderType>
RefCountedPoolImpl<Derived, ValueType, ContainerType, ReferenceCounterType, LockProviderType>::RefCountedPoolImpl(
  memory::MemoryResource memory_resource, size_t capacity)
  : avail_indexes_(memory_resource)
{
  std::pmr::polymorphic_allocator<PoolEntry> allocator(memory_resource);
  const auto storage_ptr =
    std::allocator_traits<std::pmr::polymorphic_allocator<PoolEntry>>::allocate(allocator, capacity);
  pool_entry_storage_ = std::unique_ptr<PoolEntry, std::function<void(PoolEntry*)>>{
    storage_ptr,
    [memory_resource, capacity](PoolEntry* ptr)
    {
      std::pmr::polymorphic_allocator<PoolEntry> allocator2{memory_resource};
      std::ranges::for_each(
        std::span(ptr, capacity),
        [&allocator2](auto& entry)
        { std::allocator_traits<std::pmr::polymorphic_allocator<PoolEntry>>::destroy(allocator2, &entry); });
      std::allocator_traits<std::pmr::polymorphic_allocator<PoolEntry>>::deallocate(allocator2, ptr, capacity);
    }};
  pool_entry_span_ = std::span(storage_ptr, capacity);
  avail_indexes_.reserve(capacity);
  for (size_t i = 0U; i < capacity; ++i)
  {
    allocator.construct(&pool_entry_span_[i], memory::make_non_null_from_ref(*this), i);
    avail_indexes_.emplace_back(i);
  }
}

template <
  typename Derived,
  typename ValueType,
  typename ContainerType,
  typename ReferenceCounterType,
  typename LockProviderType>
RefCountedPoolImpl<Derived, ValueType, ContainerType, ReferenceCounterType, LockProviderType>::~RefCountedPoolImpl()
{
  [[maybe_unused]] const auto guard = lock_.lock();
  if (avail_indexes_.size() != avail_indexes_.capacity())
  {
    log_cerr_error("Destroying shared object pool with outstanding references");
    std::terminate();
  }
}

template <
  typename Derived,
  typename ValueType,
  typename ContainerType,
  typename ReferenceCounterType,
  typename LockProviderType>
[[nodiscard]] size_t
RefCountedPoolImpl<Derived, ValueType, ContainerType, ReferenceCounterType, LockProviderType>::get_avail_count()
  const noexcept
{
  [[maybe_unused]] const auto guard = lock_.lock();
  return avail_indexes_.size();
}

template <
  typename Derived,
  typename ValueType,
  typename ContainerType,
  typename ReferenceCounterType,
  typename LockProviderType>
[[nodiscard]] size_t
RefCountedPoolImpl<Derived, ValueType, ContainerType, ReferenceCounterType, LockProviderType>::get_capacity()
  const noexcept
{
  [[maybe_unused]] const auto guard = lock_.lock();
  return avail_indexes_.capacity();
}

template <
  typename Derived,
  typename ValueType,
  typename ContainerType,
  typename ReferenceCounterType,
  typename LockProviderType>
[[nodiscard]] bool
RefCountedPoolImpl<Derived, ValueType, ContainerType, ReferenceCounterType, LockProviderType>::is_empty() const noexcept
{
  [[maybe_unused]] const auto guard = lock_.lock();
  return avail_indexes_.empty();
}

} // namespace jewels::detail
