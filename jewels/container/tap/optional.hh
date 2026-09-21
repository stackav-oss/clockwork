// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/container/tap/constants.hh"
#include "jewels/memory/aligned_storage.hh"
#include "jewels/meta/concepts.hh"

#include <array>
#include <cstddef>
#include <optional>
#include <type_traits>
#include <utility>

namespace jewels::tap
{

template <jewels::meta::ImplicitLifetimeType Value>

class Optional;

namespace detail
{

/// Get the size of the trailing padding.
template <jewels::meta::ImplicitLifetimeType Value>
constexpr size_t optional_trailing_padding() noexcept;

/// Layout adhering to clockwork::Tachyon specifications.
template <jewels::meta::ImplicitLifetimeType Value>

struct OptionalLayout;

/// Layout adhering to clockwork::Tachyon specifications when no padding is needed.
template <jewels::meta::ImplicitLifetimeType Value>
  requires(optional_trailing_padding<Value>() == 0UL)
struct __attribute__((packed)) alignas(constants::bool_alignment) OptionalLayout<Value>
{
  /// Underlying object storage.
  jewels::memory::AlignedStorage<Value> storage{};
  /// Whether or not an object is constructed in the storage.
  bool has_value{false};
};

/// Layout adhering to clockwork::Tachyon specifications when padding is needed.
template <jewels::meta::ImplicitLifetimeType Value>
  requires(optional_trailing_padding<Value>() > 0UL)
struct __attribute__((packed)) alignas(alignof(Value)) OptionalLayout<Value>
{
  /// Underlying object storage.
  jewels::memory::AlignedStorage<Value> storage{};
  /// Whether or not an object is constructed in the storage.
  bool has_value{false};
  /// Padding
  std::array<std::byte, optional_trailing_padding<Value>()> padding{};
};

/// Check if a type instantiates Optional.  Base case is false.
template <class T>
constexpr bool is_optional_v = false;

/// Specialization for Optional types.
template <class T>
constexpr bool is_optional_v<Optional<T>> = true;

} // namespace detail

/// Optional class that mimics std::optional but adheres to the
/// clockwork::Tachyon requirements.
template <jewels::meta::ImplicitLifetimeType Value>
class Optional
{
public:
  /// Value type.
  using value_type = Value;

  /// Default construction.
  constexpr Optional() noexcept = default;

  /// Construct from std::nullopt.
  explicit constexpr Optional(std::nullopt_t /*nullopt*/) noexcept;

  /// Copy constructor.
  constexpr Optional(const Optional<Value>&) noexcept = default;

  /// Move constructor.
  constexpr Optional(Optional<Value>&&) noexcept = default;

  /// Copy from another optional.
  /// @param other The other optional.
  template <class Other>
    requires(std::is_constructible_v<Value, const Other&> && !std::is_same_v<Value, Other>)
  explicit constexpr Optional(const Optional<Other>& other) noexcept(
    std::is_nothrow_constructible_v<Value, const Other&>);

  /// In place constructor from arguments.
  /// @param args Argument pack to forward to the constructor of Value.
  template <class... Args>
  explicit constexpr Optional(std::in_place_t /*in_place*/, Args&&... args) noexcept(
    std::is_nothrow_constructible_v<Value, Args...>);

  /// Construct from a type that is not an Optional and not Value.
  /// @param other The type to construct from.
  template <class Other>
    requires(!detail::is_optional_v<Other> && std::is_constructible_v<Value, Other &&>)
  constexpr explicit Optional(Other&& other) noexcept(std::is_nothrow_constructible_v<Value, Other&&>);

  /// Destructor
  ~Optional() noexcept = default;

  /// Copy assignment.
  constexpr Optional<Value>& operator=(const Optional<Value>&) noexcept = default;
  /// Move assignment.
  constexpr Optional<Value>& operator=(Optional<Value>&&) noexcept = default;

  /// Assign from another optional type.
  /// @param other The other type.
  template <class Other>
    requires(std::is_constructible_v<Value, const Other&> && !std::is_same_v<Other, Value>)
  constexpr Optional<Value>&
  operator=(const Optional<Other>& other) noexcept(std::is_nothrow_constructible_v<Value, const Other&>);

  /// Assign from another non-optional type.
  /// @param other The other type.
  template <class Other>
    requires(
      (std::is_constructible_v<Value, const Other&> && std::is_assignable_v<Value&, const Other&>) &&
      !detail::is_optional_v<Other>)
  constexpr Optional<Value>& operator=(const Other& other) noexcept(
    std::is_nothrow_constructible_v<Value, const Other&> && std::is_nothrow_assignable_v<Value&, const Other&>);

  /// Access the address of the value.
  /// @{
  constexpr const Value* operator->() const noexcept;
  constexpr Value* operator->() noexcept;
  /// @}

  /// Access the value by reference.
  /// @require The optional must be holding a valid value.  UB otherwise.
  /// @{
  constexpr const Value& operator*() const& noexcept;
  constexpr Value& operator*() & noexcept;
  constexpr const Value&& operator*() const&& noexcept;
  constexpr Value&& operator*() && noexcept;
  /// @}

  /// Check if a value exists.
  /// @{
  constexpr explicit operator bool() const noexcept;
  [[nodiscard]] constexpr bool has_value() const noexcept;
  /// @}

  /// Access the value for generic std::optional-compatible code.
  /// Code that names tap::Optional should use the outcome overload.
  /// @throws std::bad_optional_access if a value does not exist.
  /// @{
  [[nodiscard]] constexpr Value& value() &;
  [[nodiscard]] constexpr const Value& value() const&;
  [[nodiscard]] constexpr Value&& value() &&;
  [[nodiscard]] constexpr const Value&& value() const&&;
  /// @}

  /// Access the value without throwing.
  /// @return failure when this optional is empty; the output is unchanged on failure.
  /// @{
  constexpr jewels::BinaryOutcome value(jewels::Out<Value*> value_out) & noexcept;
  constexpr jewels::BinaryOutcome value(jewels::Out<const Value*> value_out) const& noexcept;
  constexpr jewels::BinaryOutcome
  value(jewels::FactoryOut<Value> value_out) && noexcept(std::is_nothrow_move_constructible_v<Value>);
  constexpr jewels::BinaryOutcome
  value(jewels::FactoryOut<Value> value_out) const&& noexcept(std::is_nothrow_copy_constructible_v<Value>);
  /// @}

  /// If a value exists, reutrn it, otherwise return default_value.
  /// @param default_value The value returned if one doesn't exist.
  template <class Other>
  [[nodiscard]] constexpr Value value_or(Other&& default_value) const
    noexcept(std::is_nothrow_copy_constructible_v<Value> && std::is_nothrow_constructible_v<Value, Other&&>);

  /// Swap with another optional.
  constexpr void swap(Optional<Value>& other) noexcept;

  /// Set to an empty optional.  This zeros the entire object to
  /// adhere to clockwork::Tachyon's specifications.
  constexpr void reset() noexcept;

  /// Insert an object.
  /// @param args A pack of args to construct the object.
  /// @return A reference to the constructed object.
  template <class... Args>
  constexpr Value& emplace(Args&&... args) noexcept(std::is_nothrow_constructible_v<Value, Args...>);

  /// Equality operator with an optional of the same type.
  [[nodiscard]] constexpr bool operator==(const Optional<Value>& other) const
    noexcept(noexcept(std::declval<const Value&>() == std::declval<const Value&>()));

  /// Equality operator with a value of same type.
  [[nodiscard]] constexpr bool operator==(const Value& other) const
    noexcept(noexcept(std::declval<const Value&>() == std::declval<const Value&>()));

  /// Equality operator with std::nullopt.
  [[nodiscard]] constexpr bool operator==(std::nullopt_t /*nullopt*/) const;

private:
  /// Throws std::bad_optional_access if no value exists.
  void check_bad_access() const;

  /// The underlying data.
  detail::OptionalLayout<Value> fields_;
};

template <class Value>
  requires jewels::meta::ImplicitLifetimeType<std::decay_t<Value>>
Optional<std::decay_t<Value>> make_optional(Value&& value);

template <jewels::meta::ImplicitLifetimeType Value, class... Args>
Optional<Value> make_optional(std::in_place_t /* in_place */, Args&&... args);

} // namespace jewels::tap

#include "jewels/container/tap/optional.inl"
