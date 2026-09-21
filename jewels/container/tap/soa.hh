// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"

#include <boost/iterator/iterator_facade.hpp>

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <type_traits>
#include <utility>

namespace clockwork
{
template <typename T>
struct TapInit;
}

namespace jewels::tap
{

namespace detail
{

/// Type trait to select the smallest unsigned integer type that can hold max_size.
/// This minimizes padding in SoA (Struct-of-Arrays) layouts.
template <size_t max_size>
struct CompactSizeType
{
  static_assert(max_size <= std::numeric_limits<uint64_t>::max(), "max_size is unrepresentable in uint64_t");
  using type = std::conditional_t<
    (max_size <= std::numeric_limits<uint8_t>::max()),
    uint8_t,
    std::conditional_t<
      (max_size <= std::numeric_limits<uint16_t>::max()),
      uint16_t,
      std::conditional_t<(max_size <= std::numeric_limits<uint32_t>::max()), uint32_t, uint64_t>>>;
};

} // namespace detail

/// Helper alias for the compact size type.
template <size_t max_size>
using compact_size_t = typename detail::CompactSizeType<max_size>::type;

/// Default traits for SoA types. Derived classes can specialize this to provide ElementRef
/// and ElementConstRef before the class is complete (to break CRTP circular dependency).
template <class Derived>
struct SoaTraits
{
  using ElementRef = typename Derived::ElementRef;
  using ElementConstRef = typename Derived::ElementConstRef;
};

namespace detail
{

/// Helper to select the appropriate element reference type based on ValueType constness.
template <class Derived, class ValueType>
using ToElementRef = std::conditional_t<
  std::is_const_v<ValueType>,
  typename SoaTraits<Derived>::ElementConstRef,
  typename SoaTraits<Derived>::ElementRef>;

} // namespace detail

// Forward declaration
template <class Derived, class ValueType>
class SoaIterator;

/// CRTP base class providing common SoA container operations.
/// Derived classes must provide:
/// - ElementRef type (nested struct for element access) OR specialize SoaTraits
/// - compare_fields(const Derived&, size_t count) method
/// - wipe_range(size_t begin, size_t end) method (VarSoa only)
/// - construct_range(size_t begin, size_t end) method (VarSoa only)
/// - size_ member (VarSoa only)
template <class Derived, class ElementType, size_t fixed_capacity, bool is_variable>
class SoaInterface
{
public:
  using value_type = ElementType;
  using ElementRef = typename SoaTraits<Derived>::ElementRef;
  using ElementConstRef = typename SoaTraits<Derived>::ElementConstRef;
  using size_type = size_t;
  using iterator = SoaIterator<Derived, ElementType>;
  using const_iterator = SoaIterator<Derived, const ElementType>;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  /// Access element at the given index (unchecked).
  [[nodiscard]] constexpr auto operator[](size_t index) noexcept -> ElementRef;

  /// Access element at the given index (unchecked, const).
  [[nodiscard]] constexpr auto operator[](size_t index) const noexcept -> ElementConstRef;

  /// Access element at the given index (checked via factory output parameter).
  /// @param element_out Factory output parameter that will be set to the element reference if successful
  /// @param index The index of the element to access
  /// @return success if index is valid, failure otherwise
  inline jewels::BinaryOutcome at(jewels::FactoryOut<ElementRef> element_out, size_t index) noexcept;

  /// Access element at the given index (checked via factory output parameter, const).
  /// @param element_out Factory output parameter that will be set to the element reference if successful
  /// @param index The index of the element to access
  /// @return success if index is valid, failure otherwise
  inline jewels::BinaryOutcome at(jewels::FactoryOut<ElementConstRef> element_out, size_t index) const noexcept;

  /// Access element at the given index (STL-style, throws on error).
  /// This is for generic STL-compatible code; code that names the SoA should use the BinaryOutcome version.
  /// @param index The index of the element to access
  /// @return Reference to the element
  /// @throws std::out_of_range if index >= size()
  [[nodiscard]] inline ElementRef at(size_t index);

  /// Access element at the given index (STL-style, throws on error, const).
  /// This is for generic STL-compatible code; code that names the SoA should use the BinaryOutcome version.
  /// @param index The index of the element to access
  /// @return Const reference to the element
  /// @throws std::out_of_range if index >= size()
  [[nodiscard]] inline ElementConstRef at(size_t index) const;

  /// Return the number of elements in the SoA.
  [[nodiscard]] constexpr size_t size() const noexcept;

  /// Return the capacity of the SoA.
  [[nodiscard]] constexpr size_t capacity() const noexcept;

  /// Check if the SoA is empty.
  [[nodiscard]] constexpr bool empty() const noexcept;

  /// Resize the SoA to the given size
  /// This is for generic STL-compatible code; code that names the SoA should use try_resize.
  /// Throws std::length_error if new_size > capacity().
  inline void resize(size_t new_size)
    requires(is_variable);

  /// Try to resize the SoA to the given size
  /// @param new_size The desired new size
  /// @return success if the resize succeeded, failure if new_size > capacity()
  inline jewels::BinaryOutcome try_resize(size_t new_size) noexcept(
    noexcept(std::declval<Derived&>().construct_range(size_t{}, size_t{})) &&
    noexcept(std::declval<Derived&>().wipe_range(size_t{}, size_t{})))
    requires(is_variable);

  /// Clear all elements from the SoA
  inline void clear() noexcept(noexcept(std::declval<Derived&>().wipe_range(size_t{}, size_t{})))
    requires(is_variable);

  /// Add a new default-constructed element to the end of the SoA
  /// This is for generic STL-compatible code; code that names the SoA should use try_emplace_back.
  /// Throws std::length_error if size() >= capacity().
  /// @return Reference to the newly added element
  inline ElementRef emplace_back()
    requires(is_variable);

  /// Add a new element to the end of the SoA from a TapInit.
  /// This is for generic STL-compatible code; code that names the SoA should use try_emplace_back.
  /// Throws std::length_error if size() >= capacity().
  /// @param element The TapInit to copy from
  /// @return Reference to the newly added element
  inline ElementRef emplace_back(const clockwork::TapInit<ElementType>& element)
    requires(is_variable);

  /// Add a new element to the end of the SoA from an element.
  /// This is for generic STL-compatible code; code that names the SoA should use try_emplace_back.
  /// Throws std::length_error if size() >= capacity().
  /// @param element The element to copy from
  /// @return Reference to the newly added element
  inline ElementRef emplace_back(const ElementType& element)
    requires(is_variable);

  /// Add a new element to the end of the SoA from an ElementRef.
  /// This is for generic STL-compatible code; code that names the SoA should use try_emplace_back.
  /// Throws std::length_error if size() >= capacity().
  /// @param element The ElementRef to copy from
  /// @return Reference to the newly added element
  inline ElementRef emplace_back(const ElementRef& element)
    requires(is_variable);

  /// Try to add a new default-constructed element to the end of the SoA.
  /// @param element_out Optional output parameter that will be set to the new element reference if successful
  /// @return success if the element was added, failure if size() >= capacity()
  inline jewels::BinaryOutcome
  try_emplace_back(jewels::OptionalOut<jewels::FactoryResult<ElementRef>> element_out = std::nullopt) noexcept(
    noexcept(std::declval<Derived&>().construct_element(size_t{})))
    requires(is_variable);

  /// Try to add a new element to the end of the SoA from a TapInit.
  /// @param element The TapInit to copy from
  /// @param element_out Optional output parameter that will be set to the new element reference if successful
  /// @return success if the element was added, failure if size() >= capacity()
  inline jewels::BinaryOutcome try_emplace_back(
    const clockwork::TapInit<ElementType>& element,
    jewels::OptionalOut<jewels::FactoryResult<ElementRef>> element_out =
      std::nullopt) noexcept(noexcept(std::declval<Derived&>()
                                        .construct_element(
                                          size_t{}, std::declval<const clockwork::TapInit<ElementType>&>())))
    requires(is_variable);

  /// Try to add a new element to the end of the SoA from an element.
  /// @param element The element to copy from
  /// @param element_out Optional output parameter that will be set to the new element reference if successful
  /// @return success if the element was added, failure if size() >= capacity()
  inline jewels::BinaryOutcome try_emplace_back(
    const ElementType& element,
    jewels::OptionalOut<jewels::FactoryResult<ElementRef>> element_out =
      std::nullopt) noexcept(noexcept(std::declval<Derived&>()
                                        .construct_element(size_t{}, std::declval<const ElementType&>())))
    requires(is_variable);

  /// Try to add a new element to the end of the SoA from an ElementRef.
  /// @param element The ElementRef to copy from
  /// @param element_out Optional output parameter that will be set to the new element reference if successful
  /// @return success if the element was added, failure if size() >= capacity()
  inline jewels::BinaryOutcome try_emplace_back(
    const ElementRef& element,
    jewels::OptionalOut<jewels::FactoryResult<ElementRef>> element_out =
      std::nullopt) noexcept(noexcept(std::declval<Derived&>()
                                        .construct_element(size_t{}, std::declval<const ElementRef&>())))
    requires(is_variable);

  /// Get an iterator to the beginning.
  [[nodiscard]] constexpr iterator begin() noexcept;

  /// Get an iterator to the end.
  [[nodiscard]] constexpr iterator end() noexcept;

  /// Get a const iterator to the beginning.
  [[nodiscard]] constexpr const_iterator begin() const noexcept;

  /// Get a const iterator to the end.
  [[nodiscard]] constexpr const_iterator end() const noexcept;

  /// Get a const iterator to the beginning.
  [[nodiscard]] constexpr const_iterator cbegin() const noexcept;

  /// Get a const iterator to the end.
  [[nodiscard]] constexpr const_iterator cend() const noexcept;

  /// Get a reverse iterator to the beginning.
  [[nodiscard]] inline reverse_iterator rbegin() noexcept;

  /// Get a reverse iterator to the end.
  [[nodiscard]] inline reverse_iterator rend() noexcept;

  /// Get a const reverse iterator to the beginning.
  [[nodiscard]] inline const_reverse_iterator rbegin() const noexcept;

  /// Get a const reverse iterator to the end.
  [[nodiscard]] inline const_reverse_iterator rend() const noexcept;

  /// Get a const reverse iterator to the beginning.
  [[nodiscard]] inline const_reverse_iterator crbegin() const noexcept;

  /// Get a const reverse iterator to the end.
  [[nodiscard]] inline const_reverse_iterator crend() const noexcept;

  /// Equality comparison
  [[nodiscard]] friend bool
  operator==(const Derived& lhs, const Derived& rhs) noexcept(noexcept(lhs.compare_fields(rhs, lhs.size())))
  {
    if constexpr (is_variable)
    {
      if (lhs.size() != rhs.size())
      {
        return false;
      }
    }
    return lhs.compare_fields(rhs, lhs.size());
  }

private:
  constexpr Derived& derived() noexcept
  {
    return static_cast<Derived&>(*this);
  }
  constexpr const Derived& derived() const noexcept
  {
    return static_cast<const Derived&>(*this);
  }
};

/// Iterator for SoA containers.
/// Returns ElementRef or ElementConstRef by value (lightweight proxy).
/// ValueType can be either ElementType or const ElementType.
template <class Derived, class ValueType>
class SoaIterator : public boost::iterator_facade<
                      SoaIterator<Derived, ValueType>,
                      ValueType,
                      std::random_access_iterator_tag,
                      detail::ToElementRef<Derived, ValueType>>
{
  // Match constness of Derived pointer to ValueType
  using DerivedPtr = std::conditional_t<std::is_const_v<ValueType>, const Derived*, Derived*>;

public:
  /// Default constructor.
  constexpr SoaIterator() noexcept = default;

  /// Construct from SoA pointer and index.
  /// @param soa Pointer to the SoA container
  /// @param index Index into the container
  constexpr SoaIterator(DerivedPtr soa, size_t index) noexcept
    : soa_{soa}, index_{index}
  {
  }

  /// Converting constructor from mutable iterator to const iterator.
  /// Only enabled when ValueType is const (converting to const_iterator).
  /// @param other The mutable iterator to convert from
  template <class OtherValueType = ValueType>
  constexpr explicit SoaIterator(const SoaIterator<Derived, std::remove_const_t<ValueType>>& other) noexcept
    requires(std::is_const_v<OtherValueType>)
    : soa_{other.soa_}, index_{other.index_}
  {
  }

  /// Subscript operator for random access.
  /// @param n Offset from current position
  /// @return ElementRef or ElementConstRef at offset position
  [[nodiscard]] constexpr detail::ToElementRef<Derived, ValueType> operator[](std::ptrdiff_t n) const noexcept
  {
    auto copy = *this;
    copy.advance(n);
    return copy.dereference();
  }

private:
  friend class boost::iterator_core_access;
  template <class, class>
  friend class SoaIterator;

  /// Dereference the iterator to get an element reference.
  /// @return ElementRef or ElementConstRef by value
  [[nodiscard]] constexpr detail::ToElementRef<Derived, ValueType> dereference() const noexcept
  {
    return detail::ToElementRef<Derived, ValueType>{soa_, index_};
  }

  /// Check if two iterators are equal.
  /// @param other The other iterator
  /// @return true if equal
  [[nodiscard]] constexpr bool equal(const SoaIterator& other) const noexcept
  {
    return soa_ == other.soa_ && index_ == other.index_;
  }

  /// Increment the iterator.
  constexpr void increment() noexcept
  {
    ++index_;
  }

  /// Decrement the iterator.
  constexpr void decrement() noexcept
  {
    --index_;
  }

  /// Advance the iterator by n positions.
  /// @param n Number of positions to advance (can be negative)
  constexpr void advance(std::ptrdiff_t n) noexcept
  {
    index_ += static_cast<size_t>(static_cast<std::ptrdiff_t>(index_) + n);
  }

  /// Calculate distance to another iterator.
  /// @param other The other iterator
  /// @return Distance in elements
  [[nodiscard]] constexpr std::ptrdiff_t distance_to(const SoaIterator& other) const noexcept
  {
    return static_cast<std::ptrdiff_t>(other.index_) - static_cast<std::ptrdiff_t>(index_);
  }

  DerivedPtr soa_{nullptr};
  size_t index_{0};
};

} // namespace jewels::tap

namespace clockwork
{

/// Primary template for fixed-size Struct-of-Arrays.
/// Specialized per schema type with soa_enabled: true.
template <typename T, size_t size>
struct FixedSoa;

/// Primary template for variable-size Struct-of-Arrays.
/// Specialized per schema type with soa_enabled: true.
template <typename T, size_t max_size>
struct VarSoa;

} // namespace clockwork

#include "jewels/container/tap/soa.inl"
