// IWYU pragma: private, include "jewels/shared_pool/shared_buffer_pool.hh"
#pragma once

#include "jewels/shared_pool/shared_buffer_pool.hh"

#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"

#include <algorithm>
#include <array>
#include <cstddef>
#include <exception>
#include <memory_resource>
#include <string_view>

namespace jewels::detail
{

template <size_t size, size_t alignment, template <typename, typename, typename> typename RefCountedPoolT>
[[nodiscard]] typename SharedBufferPoolImpl<size, alignment, RefCountedPoolT>::ValueType&
SharedBufferPoolImpl<size, alignment, RefCountedPoolT>::SharedReference::value()
{
  if (!this->entry_ptr_->value_container)
  {
    log_cerr_error("Referencing shared object with invalid storage, this should never happen");
    std::terminate();
  }
  return *this->entry_ptr_->value_container;
}

template <size_t size, size_t alignment, template <typename, typename, typename> typename RefCountedPoolT>
[[nodiscard]] const typename SharedBufferPoolImpl<size, alignment, RefCountedPoolT>::ValueType&
SharedBufferPoolImpl<size, alignment, RefCountedPoolT>::SharedReference::value() const
{
  if (!this->entry_ptr_->value_container)
  {
    log_cerr_error("Referencing shared object with invalid storage, this should never happen");
    std::terminate();
  }
  return *this->entry_ptr_->value_container;
}

template <size_t size, size_t alignment, template <typename, typename, typename> typename RefCountedPoolT>
void SharedBufferPoolImpl<size, alignment, RefCountedPoolT>::SharedReference::reset_storage()
{
}

template <size_t size, size_t alignment, template <typename, typename, typename> typename RefCountedPoolT>
SharedBufferPoolImpl<size, alignment, RefCountedPoolT>::SharedBufferPoolImpl(
  memory::MemoryResource memory_resource, size_t capacity)
  : RefCountedPoolType(memory_resource, capacity)
{
  auto* memory_resource_ptr = static_cast<std::pmr::memory_resource*>(memory_resource);
  std::ranges::for_each(
    this->pool_entry_span_,
    [memory_resource_ptr](auto& entry)
    {
      entry.value_container = ContainerType{
        static_cast<std::array<std::byte, size>*>(memory_resource_ptr->allocate(size, alignment)),
        [memory_resource_ptr](std::array<std::byte, size>* ptr)
        { memory_resource_ptr->deallocate(ptr, size, alignment); }};
    });
}

template <size_t size, size_t alignment, template <typename, typename, typename> typename RefCountedPoolT>
jewels::expected<typename SharedBufferPoolImpl<size, alignment, RefCountedPoolT>::SharedReference, std::string_view>
SharedBufferPoolImpl<size, alignment, RefCountedPoolT>::get_shared_buffer()
{
  [[maybe_unused]] const auto guard = this->lock_.lock();
  if (this->avail_indexes_.empty())
  {
    return jewels::unexpected("Shared buffer pool is empty");
  }
  const auto entry_index = this->avail_indexes_.back();
  this->avail_indexes_.pop_back();
  this->pool_entry_span_[entry_index].reference_count.increment();
  return SharedReference(&this->pool_entry_span_[entry_index]);
}

} // namespace jewels::detail
