// IWYU pragma: private, include "jewels/shared_pool/shared_object_pool.hh"
#pragma once

#include "jewels/shared_pool/shared_object_pool.hh"

#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"

#include <cstddef>
#include <exception>
#include <optional>
#include <string_view>
#include <utility>

namespace jewels::detail
{

template <typename ValueType, template <typename, typename, typename> typename RefCountedPoolT>
[[nodiscard]] ValueType& SharedObjectPoolImpl<ValueType, RefCountedPoolT>::SharedReference::value()
{
  if (!this->entry_ptr_->value_container)
  {
    log_cerr_error("Referencing shared object with invalid storage, this should never happen");
    std::terminate();
  }
  return this->entry_ptr_->value_container.value();
}

template <typename ValueType, template <typename, typename, typename> typename RefCountedPoolT>
[[nodiscard]] const ValueType& SharedObjectPoolImpl<ValueType, RefCountedPoolT>::SharedReference::value() const
{
  if (!this->entry_ptr_->value_container)
  {
    log_cerr_error("Referencing shared object with invalid storage, this should never happen");
    std::terminate();
  }
  return this->entry_ptr_->value_container.value();
}

template <typename ValueType, template <typename, typename, typename> typename RefCountedPoolT>
void SharedObjectPoolImpl<ValueType, RefCountedPoolT>::SharedReference::reset_storage()
{
  this->entry_ptr_->value_container = std::nullopt;
}

template <typename ValueType, template <typename, typename, typename> typename RefCountedPoolT>
SharedObjectPoolImpl<ValueType, RefCountedPoolT>::SharedObjectPoolImpl(
  memory::MemoryResource memory_resource, size_t capacity)
  : RefCountedPoolType(memory_resource, capacity)
{
}

template <typename ValueType, template <typename, typename, typename> typename RefCountedPoolT>
template <typename... Args>
jewels::expected<typename SharedObjectPoolImpl<ValueType, RefCountedPoolT>::SharedReference, std::string_view>
SharedObjectPoolImpl<ValueType, RefCountedPoolT>::make_shared_object(Args&&... args)
{
  [[maybe_unused]] const auto guard = this->lock_.lock();
  if (this->avail_indexes_.empty())
  {
    return jewels::unexpected("Shared object pool is empty");
  }
  const auto entry_index = this->avail_indexes_.back();
  this->pool_entry_span_[entry_index].value_container.emplace(std::forward<Args>(args)...);
  this->avail_indexes_.pop_back();
  this->pool_entry_span_[entry_index].reference_count.increment();
  return SharedReference(&this->pool_entry_span_[entry_index]);
}

} // namespace jewels::detail
