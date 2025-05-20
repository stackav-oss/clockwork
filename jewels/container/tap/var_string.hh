// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/container/tap/var_array.hh"

#include <cstddef>
#include <string_view>
#include <type_traits>

namespace jewels::tap
{

/// A fixed capacity string.  Has the same interface as VarArray.  See
/// the VarArrayInterface in `jewels/container/tap/var_array.hh` for the
/// interface and documentation.  This class provides additional
/// interfaces specific to a string.
/// @tparam fixed_capacity The total number of characters for the
/// string including the null terminator.  This must be strictly > 0
/// to guarantee there is always space for the null terminator.
template <size_t fixed_capacity>
class VarString : public VarArrayInterface<VarString<fixed_capacity>, char, fixed_capacity - 1UL>
{
  using Base = VarArrayInterface<VarString<fixed_capacity>, char, fixed_capacity - 1UL>;

  static_assert(fixed_capacity > 0UL, "Must be space for a null terminator.");

public:
  /// Default construct the container.
  constexpr VarString() noexcept = default;

  /// Default the copy constructor and assignment.
  /// @{
  VarString(const VarString& other) noexcept = default;
  constexpr VarString& operator=(const VarString& other) noexcept = default;
  /// @}

  /// Default the move constructor and assignment.
  /// @{
  VarString(VarString&& other) noexcept = default;
  constexpr VarString& operator=(VarString&& other) noexcept = default;
  /// @}

  /// Must have a trivial destructor.
  ~VarString() noexcept = default;

  /// Initialize from a pack of chars.
  /// @param first The first argument in the pack.
  /// @param rest The rest of the arguments.
  template <class... Rest>
    requires(std::is_same_v<std::common_type_t<char, Rest...>, char> && (sizeof...(Rest) + 1UL) < fixed_capacity)
  explicit VarString(char first, Rest... rest) noexcept;

  /// Construct from a fixed-size c-string.  This enables construction from a string literal.
  /// @tparam in_size Size of the input string including null terminator.
  /// @param in_str The input c-string.
  template <size_t in_size>
    requires(in_size <= fixed_capacity)
  // NOLINTNEXTLINE(modernize-avoid-c-arrays) This is specifically for C-style strings.
  explicit VarString(const char (&in_str)[in_size]) noexcept;

  /// Access a c-style string.
  /// @return the c-style string.
  [[nodiscard]] const char* c_str() const noexcept;

  /// Creates a string_view of this string.
  [[nodiscard]] constexpr std::string_view string_view() const noexcept;

  /// Creates a string_view of this string.
  // NOLINTNEXTLINE(google-explicit-constructor) Allow implicit conversion to match std::string's interface
  constexpr operator std::string_view() const noexcept;

  /// Attempts to set the array to the contents of the string_view
  /// NOTE: This intentionally hides VarArray::try_set(span) because spans of literal strings include the null
  /// terminator which means VarString<4>::try_set("abc") would fail.  Taking a string_view allows more 'normal'
  /// string-like behaviors.
  /// @return true if successful, false if the string_view is too large
  [[nodiscard]] constexpr bool try_set(std::string_view other) noexcept;

  /// Sets the string to the the provided string, truncating if the length exceeds the fixed capacity.
  /// @param[in] str String to set
  constexpr void set_truncate(std::string_view str) noexcept;

private:
  friend class VarArrayInterface<VarString<fixed_capacity>, char, fixed_capacity - 1UL>;

  /// All data fields.
  /// @note Add one to the capacity to account for a null terminator.
  detail::VarArrayLayout<char, fixed_capacity> fields_;
};

/// Compare two VarString for equlity
/// @param[in] lhs The left hand side string.
/// @param[in] rhs The right hand side string.
/// @return true if the strings are equal
template <size_t fixed_capacity>
bool operator==(const VarString<fixed_capacity>& lhs, const VarString<fixed_capacity>& rhs);

/// Compare two VarString for inequlity
/// @param[in] lhs The left hand side string.
/// @param[in] rhs The right hand side string.
/// @return true if the strings are not equal
template <size_t fixed_capacity>
bool operator!=(const VarString<fixed_capacity>& lhs, const VarString<fixed_capacity>& rhs);

} // namespace jewels::tap

#include "jewels/container/tap/var_string.inl"
