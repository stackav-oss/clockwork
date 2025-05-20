// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/container/tap/constants.hh"
#include "jewels/memory/aligned_storage.hh"
#include "jewels/meta/concepts.hh"
#include "jewels/std/expected.hh"

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <type_traits>

namespace jewels::tap
{

namespace detail
{

/// Calculate the padding needed between the storage and the size fields.
template <meta::ImplicitLifetimeType Value, size_t fixed_capacity>
constexpr size_t var_array_between_padding();

/// Calculate the trailing padding after the size field.
template <meta::ImplicitLifetimeType Value, size_t fixed_capacity>
constexpr size_t var_array_trailing_padding();

/// Defines the data layout for the VarArray class.
/// Forward declaration to allow specializations only.
template <meta::ImplicitLifetimeType Value, size_t fixed_capacity>
struct VarArrayLayout;

/// Specialization when no padding is needed.

template <meta::ImplicitLifetimeType Value, size_t fixed_capacity>
  requires(
    var_array_between_padding<Value, fixed_capacity>() == 0UL &&
    var_array_trailing_padding<Value, fixed_capacity>() == 0UL)
struct __attribute__((packed)) alignas(std::max(alignof(Value), constants::size_alignment))
  VarArrayLayout<Value, fixed_capacity>
{
  static_assert(fixed_capacity > 0, "If capacity == 0, then the layout is not compliant with clockwork::Tachyon.");
  /// Underlying object storage.
  std::array<memory::AlignedStorage<Value>, fixed_capacity> storage{};
  /// Number of construted elements currently in the storage.
  size_t size{0UL};
};

/// Specialization when padding between fields is needed.
template <meta::ImplicitLifetimeType Value, size_t fixed_capacity>
  requires(
    var_array_between_padding<Value, fixed_capacity>() > 0UL &&
    var_array_trailing_padding<Value, fixed_capacity>() == 0UL)
struct __attribute__((packed)) alignas(std::max(alignof(Value), constants::size_alignment))
  VarArrayLayout<Value, fixed_capacity>
{
  static_assert(fixed_capacity > 0, "If capacity == 0, then the layout is not compliant with clockwork::Tachyon.");
  /// Underlying object storage.
  std::array<memory::AlignedStorage<Value>, fixed_capacity> storage{};
  /// Padding
  std::array<std::byte, var_array_between_padding<Value, fixed_capacity>()> padding{};
  /// Number of construted elements currently in the storage.
  size_t size{0UL};
};

/// Specialization when trailing padding is needed.
template <meta::ImplicitLifetimeType Value, size_t fixed_capacity>
  requires(
    var_array_between_padding<Value, fixed_capacity>() == 0UL &&
    var_array_trailing_padding<Value, fixed_capacity>() > 0UL)
struct __attribute__((packed)) alignas(std::max(alignof(Value), constants::size_alignment))
  VarArrayLayout<Value, fixed_capacity>
{
  static_assert(fixed_capacity > 0, "If capacity == 0, then the layout is not compliant with clockwork::Tachyon.");
  /// Underlying object storage.
  std::array<memory::AlignedStorage<Value>, fixed_capacity> storage{};
  /// Number of construted elements currently in the storage.
  size_t size{0UL};
  /// Padding
  std::array<std::byte, var_array_trailing_padding<Value, fixed_capacity>()> padding{};
};

} // namespace detail

/// A fixed capacity array.
/// @tparam Derived A derived class for CRTP.  Must provide a `fields_` member of type VarArrayLayout.
/// @tparam Value The value type.
/// @tparam fixed_capacity A maximum number of elements to store.
template <class Derived, meta::ImplicitLifetimeType Value, size_t fixed_capacity>
class VarArrayInterface
{
private:
  /// Default construct the container.
  constexpr VarArrayInterface() = default;

  /// Copy the contents of the vector.
  VarArrayInterface(const VarArrayInterface& other) noexcept = default;

  /// Types are trivially destructible and likely will not benefit
  /// from move semantics.  In particular, no Clockwork Schema types
  /// will benefit from move semantics.
  VarArrayInterface(VarArrayInterface&& other) noexcept = default;

public:
  /// Usual member types
  using value_type = Value;
  using reference = Value&;
  using const_reference = const Value&;
  using pointer = Value*;
  using const_pointer = const Value*;
  using iterator = std::span<Value>::iterator;
  using const_iterator = std::span<const Value>::iterator;
  using size_type = size_t;
  using difference_type = std::ptrdiff_t;

  VarArrayInterface& operator=(const VarArrayInterface& other) noexcept = default;

  VarArrayInterface& operator=(VarArrayInterface&& other) noexcept = default;

  /// Must have a trivial destructor.
  ~VarArrayInterface() = default;

  /// Access an element by index.
  /// @throws An exception for out of bounds access.
  /// @param index The position to access.
  /// @return A qualified reference to the element.
  /// @{
  [[nodiscard]] reference at(size_t index);
  [[nodiscard]] const_reference at(size_t index) const;
  /// @}

  /// Access without bounds checking.
  /// @param index The position to access.
  /// @return A qualified reference to the element.
  [[nodiscard]] reference operator[](size_t index);
  [[nodiscard]] const_reference operator[](size_t index) const;

  /// Access iterators.
  /// @return An iterator.
  /// @{
  [[nodiscard]] iterator begin();
  [[nodiscard]] const_iterator begin() const;
  [[nodiscard]] iterator end();
  [[nodiscard]] const_iterator end() const;
  /// @}

  /// Get the current size.
  /// @return The size.
  [[nodiscard]] constexpr size_type size() const;

  /// Get the capacity.
  /// @return The capacity.
  [[nodiscard]] constexpr size_type capacity() const;

  /// Remove all elements.
  constexpr void clear();

  /// Pointer to the storage buffer.
  /// @return A qualified pointer to the start of the buffer.
  /// @{
  [[nodiscard]] pointer data();
  [[nodiscard]] const_pointer data() const;
  /// @}

  /// Reserve space in the container.
  /// For compatibility with generic code.
  void reserve(size_t new_capacity);

  /// Resize the container if possible.
  /// @throws std::length_error if the size exceeds the capacity.
  /// @param new_size The resulting size of the container.
  void resize(size_t new_size);

  /// Resize the container if possible.
  /// @throws std::length_error if the size exceeds the capacity.
  /// @param new_size The resulting size of the container.
  /// @param new_value A value to set any new elements to if the size grows.
  void resize(size_t new_size, const_reference new_value);

  /// Insert a new element
  /// @throws std::length_error if a new element would result in a size
  /// exceeding the capacity.
  /// @param args A pack of args to construct the new value from.
  template <class... Args>
  void push_back(Args&&... args);

  /// Insert a new element
  /// @throws std::length_error if a new element would result in a size
  /// exceeding the capacity.
  /// @param args A pack of args to construct the new value from.
  /// @param A reference to the newly inserted element.
  template <class... Args>
  reference emplace_back(Args&&... args);

  /// Try to insert a new element.
  /// @param args A pack of args to construct the new value from.
  /// @param Return an iterator to the new element or an error code on failure.
  template <class... Args>
  [[nodiscard]] jewels::expected<iterator, jewels::MonoError> try_emplace_back(Args&&... args);

  /// Remove the last element.
  void pop_back();

  /// Try to remove the last element.
  /// @return True if an element was removed.
  [[nodiscard]] bool try_pop_back();

  /// Return true if the container is full.
  /// @return True if full and false otherwise.
  [[nodiscard]] constexpr bool full() const;

  /// Return true if the container is empty.
  /// @return True if empty and false otherwise.
  [[nodiscard]] constexpr bool empty() const;

  /// Erase the element at pos.
  /// @{
  iterator erase(iterator pos);
  iterator erase(const_iterator pos);
  /// @}

  /// Erase a range of elements [first, last).
  /// @{
  iterator erase(iterator first, iterator last);
  iterator erase(const_iterator first, const_iterator last);
  /// @}

  /// Insert an element at pos.
  /// @throws std::length_error if a new element would result in a size
  /// exceeding the capacity.
  template <class NewValue>
  iterator insert(iterator pos, NewValue&& new_value);

  /// Insert a range of elements at pos.
  /// @throws std::length_error if the resulting size would exceeding
  /// the capacity.
  template <class InputIter>
  iterator insert(iterator pos, InputIter first, InputIter last);

  /// Attempts to set the array to the contents of the provided span
  /// @return true if successful, false if the span's length the fixed_capacity
  [[nodiscard]] constexpr bool try_set(std::span<const value_type> other) noexcept;

  /// Get a span to the elements.
  /// @return A span.
  /// @{
  [[nodiscard]] std::span<value_type> span();
  [[nodiscard]] std::span<const value_type> span() const;
  /// @}

protected:
  /// Common implementation for resize.
  /// @throws std::length_error if the size exceeds the capacity.
  /// @params new_size The size to resize to.
  /// @params args Either an empty pack or a const_reference.
  template <class... Args>
    requires(sizeof...(Args) <= 1 && (sizeof...(Args) == 0 || (std::is_same_v<Args, Value> && ...)))
  void resize_impl(size_t new_size, const Args&... args);

  /// Assumes the capacity is adequate for the insertion.  Does not
  /// validate size and therefore does not throw.
  template <class InputIter>
  iterator insert_impl(iterator pos, InputIter first, InputIter last) noexcept;

  /// Zero out any elements no longer used.
  /// @param count The number of elements at the end to wipe.
  void wipe(size_t count);

  /// Get the fields from the derived class.
  /// @return The fields.
  /// @{
  [[nodiscard]] constexpr auto& fields();
  [[nodiscard]] constexpr const auto& fields() const;
  /// @}

  friend Derived;
};

/// A fixed capacity contiguous array.  The bulk of the interface is
/// in VarArrayInterface to allow reuse.  See that class above for
/// documentation and details.
template <meta::ImplicitLifetimeType Value, size_t fixed_capacity>
class VarArray : public VarArrayInterface<VarArray<Value, fixed_capacity>, Value, fixed_capacity>
{
public:
  /// Default construct the container.
  constexpr VarArray() = default;

  /// Copy the contents of the vector.
  /// @{
  VarArray(const VarArray& other) noexcept = default;
  VarArray& operator=(const VarArray&) = default;
  /// @}

  /// Types are trivially destructible and likely will not benefit
  /// from move semantics.  In particular, no Clockwork Schema types
  /// will benefit from move semantics.
  /// @{
  VarArray(VarArray&& other) noexcept = default;
  VarArray& operator=(VarArray&& other) = default;
  /// @}

  /// Must have a trivial destructor.
  ~VarArray() = default;

  /// Initialize from a pack of elements.  A Value type must be
  /// constructible from each argument type.
  /// @param first The first argument in the pack.
  /// @param rest The rest of the arguments.
  template <class First, class... Rest>
  explicit VarArray(First&& first, Rest&&... rest)
    requires(!std::is_same_v<std::decay_t<First>, VarArray<Value, fixed_capacity>>);

private:
  friend class VarArrayInterface<VarArray<Value, fixed_capacity>, Value, fixed_capacity>;

  /// All data fields.
  detail::VarArrayLayout<Value, fixed_capacity> fields_;
};

/// Compare two VarArray for equlity
/// @param[in] lhs The left hand side array.
/// @param[in] rhs The right hand side array.
/// @return true if the strings are equal
template <typename T, size_t fixed_capacity>
bool operator==(const VarArray<T, fixed_capacity>& lhs, const VarArray<T, fixed_capacity>& rhs);

/// Compare two VarArray for inequlity
/// @param[in] lhs The left hand side array.
/// @param[in] rhs The right hand side array.
/// @return true if the strings are not equal
template <typename T, size_t fixed_capacity>
bool operator!=(const VarArray<T, fixed_capacity>& lhs, const VarArray<T, fixed_capacity>& rhs);

} // namespace jewels::tap

#include "jewels/container/tap/var_array.inl"
