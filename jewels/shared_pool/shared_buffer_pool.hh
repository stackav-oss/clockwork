// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/memory/memory_resource.hh"
#include "jewels/shared_pool/ref_counted_pool.hh"
#include "jewels/std/expected.hh"

#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <string_view>

namespace jewels
{

namespace detail
{

/// Value type for the buffers stored in the pool
template <size_t size>
using BufferValueType = std::array<std::byte, size>;

/// Container type for the buffers stored in the pool
template <size_t size>
using BufferContainerType = std::unique_ptr<BufferValueType<size>, std::function<void(BufferValueType<size>*)>>;

/// Base implementation of a pool of reference counted shared buffers
/// @tparam size Buffer size in bytes
/// @tparam alignment Buffer alignment in bytes
/// @tparam RefCountedPoolT Reference counted pool type
template <size_t size, size_t alignment, template <typename, typename, typename> typename RefCountedPoolT>
class SharedBufferPoolImpl : public RefCountedPoolT<
                               SharedBufferPoolImpl<size, alignment, RefCountedPoolT>,
                               BufferValueType<size>,
                               BufferContainerType<size>>
{
public:
  using ValueType = BufferValueType<size>;
  using ContainerType = BufferContainerType<size>;

  using RefCountedPoolType =
    RefCountedPoolT<SharedBufferPoolImpl<size, alignment, RefCountedPoolT>, ValueType, ContainerType>;
  using PoolEntryType = typename RefCountedPoolType::PoolEntry;

  friend RefCountedPoolType;

  /// Buffer size
  static constexpr auto buffer_size = size;

  /// Buffer alignment
  static constexpr auto buffer_alignment = alignment;

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
  SharedBufferPoolImpl(memory::MemoryResource memory_resource, size_t capacity);

  ~SharedBufferPoolImpl() = default;

  SharedBufferPoolImpl(const SharedBufferPoolImpl&) = delete;
  SharedBufferPoolImpl& operator=(const SharedBufferPoolImpl&) = delete;
  SharedBufferPoolImpl(SharedBufferPoolImpl&&) = delete;
  SharedBufferPoolImpl& operator=(SharedBufferPoolImpl&&) = delete;

  /// Get a buffer from the pool
  /// @return Reference counted buffer or error message on failure
  jewels::expected<SharedReference, std::string_view> get_shared_buffer();
};

} // namespace detail

/// Non thread safe pool of reference counted shared buffers
/// @tparam size Buffer size in bytes
/// @tparam alignment Buffer alignment in bytes
template <size_t size, size_t alignment>
using SharedBufferPool = detail::SharedBufferPoolImpl<size, alignment, RefCountedPool>;

/// Thread safe pool of reference counted shared buffers
/// @tparam size Buffer size in bytes
/// @tparam alignment Buffer alignment in bytes
template <size_t size, size_t alignment>
using ThreadSafeSharedBufferPool = detail::SharedBufferPoolImpl<size, alignment, ThreadSafeRefCountedPool>;

} // namespace jewels

#include "jewels/shared_pool/shared_buffer_pool.inl"
