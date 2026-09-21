// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>
#include <cstddef>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

namespace jewels::container
{

/// This provides a string type with local storage (i.e. it doesn't allocate).  It supports variable logical size up to
/// `max_length` but will always use the same amount of memory. The string is guaranteed to be null terminated, meaning
/// that `data[size()] == CharT()`.  Note that while the terminator is guaranteed to be present (and part of the
/// `data()` array, from a C memory access perspective), it is not considered to be part of the contained data.  That
/// is, while `(*this)[size()]` will be the null terminator, `at(size())` will throw an exception and `end()` will point
/// to the null terminator but should not be dereferenced.
template <size_t max_length, class CharT, class Traits = std::char_traits<CharT>>
class BasicBoundedString
{
public:
  using traits_type = Traits;
  using value_type = CharT;
  using pointer = CharT*;
  using difference_type = std::pointer_traits<pointer>::difference_type;
  using size_type = std::make_unsigned_t<difference_type>;
  using const_pointer = std::pointer_traits<pointer>::template rebind<const value_type>;
  using reference = value_type&;
  using const_reference = const value_type&;
  using iterator = value_type*;
  using const_iterator = const value_type*;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = const std::reverse_iterator<iterator>;

  using string_view = std::basic_string_view<CharT, Traits>;

  template <size_t other_max>
  using other_bounded_string = BasicBoundedString<other_max, CharT, Traits>;

  /// Creates a zero-size string
  constexpr BasicBoundedString() noexcept;

  /// Disallow creation from null pointers
  BasicBoundedString(std::nullptr_t) = delete;

  /// Copy constructor, which includes support for copying from `BasicBoundedStrings` strings of equal or smaller
  /// `max_length`
  template <size_t other_max>
  explicit constexpr BasicBoundedString(const other_bounded_string<other_max>& other) noexcept
    requires(other_max <= max_length);

  /// Creates a bounded string from a `string_view`, truncating if the string is too long
  static constexpr BasicBoundedString truncated(string_view string) noexcept;

  /// Returns a bounded string copy of the given string if the length is less than or equal to `max_length` or an empty
  /// optional otherwise
  static constexpr std::optional<BasicBoundedString> try_make(string_view string) noexcept;

  /// Assignment operator, which includes support for copying from `BasicBoundedStrings` strings of equal or smaller
  /// `max_length`
  template <size_t other_max>
  constexpr BasicBoundedString& operator=(const other_bounded_string<other_max>& other) noexcept
    requires(other_max <= max_length);

  /// Returns a `string_view` referencing the contents of the buffer.  Note that the `string_view` doesn't technically
  /// include the null terminator because the `string_view` is scoped to the proper size (i.e. `size()`) of the string
  /// and thus doesn't include the null terminator which is the located at `size() + 1`
  [[nodiscard]] constexpr string_view to_string_view() const noexcept
  {
    return string_view(data_.data(), size_);
  }

  /// Allow implicit conversion to a `string_view`, matching `std::string`.
  // NOLINTNEXTLINE(google-explicit-constructor) Allowing implicit conversion to string_view
  constexpr operator string_view() const noexcept
  {
    return to_string_view();
  }

  /// Returns the number of elements in the container
  [[nodiscard]] constexpr size_type size() const noexcept
  {
    return size_;
  }

  /// Returns the number of elements in the container
  [[nodiscard]] constexpr size_type length() const noexcept
  {
    return size_;
  }

  /// Returns the maximum size supported.  This is a fixed size based on the template parameters
  [[nodiscard]] static constexpr size_type max_size() noexcept
  {
    return max_length;
  }

  /// Checks if the container has no elements, i.e. if size is 0
  [[nodiscard]] constexpr bool empty() const noexcept
  {
    return size_ == 0;
  }

  /// Returns a reference to the element at specified location without bounds checking
  [[nodiscard]] constexpr const_reference operator[](size_type pos) const noexcept
  {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) This API is for a non-checked call
    return data_[pos];
  }

  /// Pointer to the underlying data.  Guaranteed to be null terminated, though not guaranteed to have no other nulls
  [[nodiscard]] constexpr const_pointer data() const noexcept
  {
    return data_.data();
  }

  /// Pointer to the underlying data.  Guaranteed to be null terminated, though not guaranteed to have no other nulls
  [[nodiscard]] constexpr const_pointer c_str() const noexcept
  {
    return data();
  }

  /// Attempts to concatenate the provided string to this.  If the resulting string would exceed max_length this is
  /// unchanged and false is returned.  Otherwise, returns true.
  [[nodiscard]] constexpr bool try_concat(std::string_view tail) noexcept;

  /// Equality operator.
  template <size_t other_size>
  [[nodiscard]] constexpr bool operator==(const BasicBoundedString<other_size, CharT, Traits>& other) const noexcept;

private:
  /// Internal constructor, creates the bounded string from the data and size, silently truncating if size exceeds
  /// max_length. `data` isn't assumed to be null terminated
  constexpr BasicBoundedString(const CharT* data, size_t size);

  size_t size_ = 0;
  std::array<char, max_length + 1> data_{};
};

template <size_t max_length>
using BoundedString = BasicBoundedString<max_length, char>;

/// For fmt compatibility
template <size_t max_length, class CharT, class Traits>
auto format_as(const BasicBoundedString<max_length, CharT, Traits>& input);

} // namespace jewels::container

#include "jewels/container/bounded_string.inl"
