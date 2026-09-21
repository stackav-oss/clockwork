// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/memory/memory_resource.hh"
#include "jewels/shared_pool/ref_counted_pool.hh"
#include "jewels/std/expected.hh"

#include <cstddef>
#include <optional>
#include <string_view>

namespace jewels
{

namespace detail
{

/// Base implementation of a pool of reference counted shared objects
/// @tparam ValueType Type of the values stored in the pool
/// @tparam RefCountedPoolT Reference counted pool type
template <typename ValueType, template <typename, typename, typename> typename RefCountedPoolT>
class SharedObjectPoolImpl
  : public RefCountedPoolT<SharedObjectPoolImpl<ValueType, RefCountedPoolT>, ValueType, std::optional<ValueType>>
{
public:
  using RefCountedPoolType =
    RefCountedPoolT<SharedObjectPoolImpl<ValueType, RefCountedPoolT>, ValueType, std::optional<ValueType>>;
  using PoolEntryType = typename RefCountedPoolType::PoolEntry;

  friend RefCountedPoolType;

  /// Reference to an object allocated from the pool
  class SharedReference : public RefCountedPoolType::SharedReferenceImpl
  {
  public:
    friend typename RefCountedPoolType::SharedReferenceImpl;

    using RefCountedPoolType::SharedReferenceImpl::SharedReferenceImpl;

    /// Unchecked accessor for the referenced value
    /// @return Reference to the referenced object
    [[nodiscard]] ValueType& value();

    /// Unchecked accessor for the referenced object
    /// @return Reference to the referenced object
    [[nodiscard]] const ValueType& value() const;

  private:
    /// Reset the storage for the referenced object when the last reference is released
    void reset_storage();
  };

  /// Constructor
  /// @param[in] memory_resource Memory resource used to allocate storage for the pool
  /// @param[in] capacity Number of entries to allocate for the pool
  SharedObjectPoolImpl(memory::MemoryResource memory_resource, size_t capacity);

  ~SharedObjectPoolImpl() = default;

  SharedObjectPoolImpl(const SharedObjectPoolImpl&) = delete;
  SharedObjectPoolImpl& operator=(const SharedObjectPoolImpl&) = delete;
  SharedObjectPoolImpl(SharedObjectPoolImpl&&) = delete;
  SharedObjectPoolImpl& operator=(SharedObjectPoolImpl&&) = delete;

  /// Allocate a references counted object from the pool
  /// @tparam Args Constructor argument types.
  /// @param[in] args Constructor arguments.
  /// @return Reference counted object or error message on failure
  /// @throws Exceptions thrown byt the referenced objects constructor
  template <typename... Args>
  jewels::expected<SharedReference, std::string_view> make_shared_object(Args&&... args);
};

} // namespace detail

/// Non thread safe pool of reference counted shared objects
/// @tparam ValueType Type of the values stored in the pool
template <typename ValueType>
using SharedObjectPool = detail::SharedObjectPoolImpl<ValueType, RefCountedPool>;

/// Thread safe pool of reference counted shared objects
/// @tparam ValueType Type of the values stored in the pool
template <typename ValueType>
using ThreadSafeSharedObjectPool = detail::SharedObjectPoolImpl<ValueType, ThreadSafeRefCountedPool>;

} // namespace jewels

#include "jewels/shared_pool/shared_object_pool.inl"
