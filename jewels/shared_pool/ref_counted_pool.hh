// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/shared_pool/detail/lock_provider.hh"
#include "jewels/shared_pool/detail/reference_counter.hh"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace jewels
{

namespace detail
{

/// Base implementation of a pool of reference counted shared objects
/// @tparam Derived Derived class
/// @tparam ValueType Type of the values stored in the pool
/// @tparam ContainerType Type of the container used for the values stored in the pool
/// @tparam ReferenceCounterType Reference counter type
/// @tparam LockProviderType Lock provider type
template <
  typename Derived,
  typename ValueType,
  typename ContainerType,
  typename ReferenceCounterType,
  typename LockProviderType>
class RefCountedPoolImpl
{
  friend Derived;

  /// Element in the pool of reference counted objects
  struct PoolEntry
  {
    /// Constructor
    /// @param[in] pool_ptr_in Pointer to the pool that contains this entry
    /// @param[in] pool_index_in Index of the entry in the pool that contains this entry
    PoolEntry(
      memory::ObjectPtr<RefCountedPoolImpl<Derived, ValueType, ContainerType, ReferenceCounterType, LockProviderType>>
        pool_ptr_in,
      uint32_t pool_index_in);

    /// Pointer to the pool that contains this object
    memory::ObjectPtr<RefCountedPoolImpl<Derived, ValueType, ContainerType, ReferenceCounterType, LockProviderType>>
      pool_ptr;

    /// Index of the entry in the backing storage pool
    uint32_t pool_index;

    /// Reference count
    ReferenceCounterType reference_count;

    /// Value when the entry is in use
    ContainerType value_container{};
  };

public:
  /// Base class for a reference to an object allocated from the pool,
  /// this class is overridden in the Derived class to get the value
  /// from its container.
  class SharedReferenceImpl
  {
    friend class RefCountedPoolImpl;
    friend Derived;
    friend typename Derived::SharedReference;

  public:
    /// Constructor to create a shared reference from an entry in the referenced pool storage
    /// @param[in] entry_ptr Pointer to the referenced pool entry
    explicit SharedReferenceImpl(PoolEntry* entry_ptr = nullptr);

    /// Destructor releases the reference to the shared object
    ~SharedReferenceImpl();

    /// Copy constructor adds a reference to the shared object
    /// @param[in] Source shared reference
    SharedReferenceImpl(const SharedReferenceImpl& other) noexcept;

    /// Copy assignment adds a reference to the shared object
    /// @param[in] Source shared reference
    SharedReferenceImpl& operator=(const SharedReferenceImpl& other) noexcept;

    /// Move constructor takes ownership of the reference to the shared object
    /// @param[in,out] Source shared reference
    SharedReferenceImpl(SharedReferenceImpl&& other) noexcept;

    /// Move assignment takes ownership of the reference to the shared object
    /// @param[in,out] Source shared reference
    SharedReferenceImpl& operator=(SharedReferenceImpl&& other) noexcept;

    /// Unchecked accessor for the referenced object
    /// @return Reference to the referenced object
    [[nodiscard]] ValueType& operator*();

    /// Unchecked accessor for the referenced object
    /// @return Reference to the referenced object
    [[nodiscard]] const ValueType& operator*() const;

    /// Unchecked accessor for the referenced object
    /// @return Pointer to the referenced object
    [[nodiscard]] ValueType* operator->();

    /// Unchecked accessor for the referenced object
    /// @return Pointer to the referenced object
    [[nodiscard]] const ValueType* operator->() const;

    /// Test whether the referenced object is valid
    /// @return True if the referenced object is valid
    [[nodiscard]] explicit operator bool() const;

    /// Test whether the referenced object is valid
    /// @return True if the referenced object is valid
    [[nodiscard]] bool is_valid() const;

    /// Get the number of references to the shared object
    /// @return Number of references to the shared object or zero if the reference is invalid
    [[nodiscard]] uint32_t get_reference_count() const;

    /// Reset the reference to the referenced object and release ownership
    void reset();

    /// Return a pointer to the referenced pool entry and release the ownership
    ///
    /// Use this to pass the reference into the user data of an io_uring request.
    ///
    /// @return Pointer to the referenced pool entry
    RefCountedPoolImpl::PoolEntry* release();

    /// Comparison operator
    /// @param[in] lhs Left hand side operand
    /// @param[in] rhs Right hand side operand
    /// @return True iff lhs == rhs
    [[nodiscard]] friend bool operator==(const SharedReferenceImpl& lhs, const SharedReferenceImpl& rhs) noexcept
    {
      return lhs.entry_ptr_ == rhs.entry_ptr_;
    }

    /// Comparison operator
    /// @param[in] lhs Left hand side operand
    /// @param[in] rhs Right hand side operand
    /// @return True iff lhs != rhs
    [[nodiscard]] friend bool operator!=(const SharedReferenceImpl& lhs, const SharedReferenceImpl& rhs) noexcept
    {
      return lhs.entry_ptr_ != rhs.entry_ptr_;
    }

  private:
    /// Pointer to the referenced pool entry
    RefCountedPoolImpl::PoolEntry* entry_ptr_{nullptr};
  };

private:
  /// Constructor
  /// @param[in] memory_resource Memory resource used to allocate storage for the pool
  /// @param[in] capacity Number of entries to allocate for the pool
  RefCountedPoolImpl(memory::MemoryResource memory_resource, size_t capacity);

public:
  /// Destructor, requires that there are no outstanding references from the pool
  ~RefCountedPoolImpl();

  // NOLINTNEXTLINE(bugprone-crtp-constructor-accessibility) Conflicts with modernize-use-equals-delete
  RefCountedPoolImpl(const RefCountedPoolImpl&) = delete;
  // NOLINTNEXTLINE(bugprone-crtp-constructor-accessibility) Conflicts with modernize-use-equals-delete
  RefCountedPoolImpl(RefCountedPoolImpl&&) = delete;
  RefCountedPoolImpl& operator=(RefCountedPoolImpl&&) = delete;
  RefCountedPoolImpl& operator=(const RefCountedPoolImpl&) = delete;

  /// Get the number of available pool entries
  /// @return Number of available entries
  [[nodiscard]] size_t get_avail_count() const noexcept;

  /// Get the total number of entries in the pool
  /// @return Total number of entries
  [[nodiscard]] size_t get_capacity() const noexcept;

  /// Test whether there are any available objects in the pool
  /// @return True iff there are no available objects
  [[nodiscard]] bool is_empty() const noexcept;

private:
  /// Allocated storage for the entries in the pool
  std::unique_ptr<PoolEntry, std::function<void(PoolEntry*)>> pool_entry_storage_;

  /// Span for the entries in the pool
  std::span<PoolEntry> pool_entry_span_;

  /// Indexes of the available pool entries
  std::pmr::vector<uint32_t> avail_indexes_;

  /// Lock provider for thread safe implementations
  mutable LockProviderType lock_;
};

} // namespace detail

/// Non-thread safe pool of reference counted objects
/// @tparam Derived Derived class
/// @tparam ValueType Type of the values stored in the pool
/// @tparam ContainerType Type of the container used for the values stored in the pool
/// @tparam ReferenceCounterType Reference counter type
/// @tparam LockProviderType Lock provider type
template <typename Derived, typename ValueType, typename ContainerType>
using RefCountedPool =
  detail::RefCountedPoolImpl<Derived, ValueType, ContainerType, detail::ReferenceCounter, detail::NullLockProvider>;

/// Thread safe pool of reference counted objects
/// @tparam Derived Derived class
/// @tparam ValueType Type of the values stored in the pool
/// @tparam ContainerType Type of the container used for the values stored in the pool
/// @tparam ReferenceCounterType Reference counter type
/// @tparam LockProviderType Lock provider type
template <typename Derived, typename ValueType, typename ContainerType>
using ThreadSafeRefCountedPool = detail::
  RefCountedPoolImpl<Derived, ValueType, ContainerType, detail::AtomicReferenceCounter, detail::MutexLockProvider>;

} // namespace jewels

#include "jewels/shared_pool/ref_counted_pool.inl"
