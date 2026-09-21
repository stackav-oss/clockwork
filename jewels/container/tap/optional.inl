// IWYU pragma: private, include "jewels/container/tap/optional.hh"
#pragma once

#include "jewels/container/tap/optional.hh"

#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/container/tap/constants.hh"
#include "jewels/memory/aligned_storage.hh"
#include "jewels/meta/concepts.hh"
#include "jewels/std/span.hh"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <type_traits>
#include <utility>

namespace jewels::tap
{

namespace detail
{

template <jewels::meta::ImplicitLifetimeType Value>
constexpr size_t optional_trailing_padding() noexcept
{
  static_assert(constants::bool_alignment == 1UL);
  return alignof(Value) - constants::bool_alignment;
}

} // namespace detail

template <jewels::meta::ImplicitLifetimeType Value>
constexpr Optional<Value>::Optional(std::nullopt_t /*nullopt*/) noexcept
{
}

template <jewels::meta::ImplicitLifetimeType Value>
template <class Other>
  requires(std::is_constructible_v<Value, const Other&> && !std::is_same_v<Value, Other>)
constexpr Optional<Value>::Optional(const Optional<Other>& other) noexcept(
  std::is_nothrow_constructible_v<Value, const Other&>)
{
  if (other.has_value())
  {
    emplace(other.value());
  }
}

template <jewels::meta::ImplicitLifetimeType Value>
template <class... Args>
constexpr Optional<Value>::Optional(std::in_place_t /*in_place*/, Args&&... args) noexcept(
  std::is_nothrow_constructible_v<Value, Args...>)
{
  emplace(std::forward<Args>(args)...);
}

template <jewels::meta::ImplicitLifetimeType Value>
template <class Other>
  requires(!detail::is_optional_v<Other> && std::is_constructible_v<Value, Other &&>)
constexpr Optional<Value>::Optional(Other&& other) noexcept(std::is_nothrow_constructible_v<Value, Other&&>)
{
  emplace(std::forward<Other>(other));
}

template <jewels::meta::ImplicitLifetimeType Value>
template <class Other>
  requires(std::is_constructible_v<Value, const Other&> && !std::is_same_v<Other, Value>)
constexpr Optional<Value>&
Optional<Value>::operator=(const Optional<Other>& other) noexcept(std::is_nothrow_constructible_v<Value, const Other&>)
{
  if (other.has_value())
  {
    emplace(*other);
  }
  else
  {
    reset();
  }
  return *this;
}

template <jewels::meta::ImplicitLifetimeType Value>
template <class Other>
  requires(
    (std::is_constructible_v<Value, const Other&> && std::is_assignable_v<Value&, const Other&>) &&
    !detail::is_optional_v<Other>)
constexpr Optional<Value>& Optional<Value>::operator=(const Other& other) noexcept(
  std::is_nothrow_constructible_v<Value, const Other&> && std::is_nothrow_assignable_v<Value&, const Other&>)
{
  if (has_value())
  {
    **this = other;
  }
  else
  {
    emplace(other);
  }
  return *this;
}

template <jewels::meta::ImplicitLifetimeType Value>
constexpr const Value* Optional<Value>::operator->() const noexcept
{
  return jewels::memory::ObjectPolicy<Value>::ptr(fields_.storage);
}

template <jewels::meta::ImplicitLifetimeType Value>
constexpr Value* Optional<Value>::operator->() noexcept
{
  return jewels::memory::ObjectPolicy<Value>::ptr(fields_.storage);
}

template <jewels::meta::ImplicitLifetimeType Value>
constexpr const Value& Optional<Value>::operator*() const& noexcept
{
  return *operator->();
}

template <jewels::meta::ImplicitLifetimeType Value>
constexpr Value& Optional<Value>::operator*() & noexcept
{
  return *operator->();
}

template <jewels::meta::ImplicitLifetimeType Value>
constexpr const Value&& Optional<Value>::operator*() const&& noexcept
{
  return std::move(*operator->());
}

template <jewels::meta::ImplicitLifetimeType Value>
constexpr Value&& Optional<Value>::operator*() && noexcept
{
  return std::move(*operator->());
}

template <jewels::meta::ImplicitLifetimeType Value>
constexpr Optional<Value>::operator bool() const noexcept
{
  return has_value();
}

template <jewels::meta::ImplicitLifetimeType Value>
constexpr bool Optional<Value>::has_value() const noexcept
{
  return fields_.has_value;
}

template <jewels::meta::ImplicitLifetimeType Value>
constexpr Value& Optional<Value>::value() &
{
  check_bad_access();
  return **this;
}

template <jewels::meta::ImplicitLifetimeType Value>
constexpr const Value& Optional<Value>::value() const&
{
  check_bad_access();
  return **this;
}

template <jewels::meta::ImplicitLifetimeType Value>
constexpr Value&& Optional<Value>::value() &&
{
  check_bad_access();
  return *std::move(*this);
}

template <jewels::meta::ImplicitLifetimeType Value>
constexpr const Value&& Optional<Value>::value() const&&
{
  check_bad_access();
  return *std::move(*this);
}

template <jewels::meta::ImplicitLifetimeType Value>
constexpr jewels::BinaryOutcome Optional<Value>::value(jewels::Out<Value*> value_out) & noexcept
{
  if (!has_value())
  {
    return jewels::failure;
  }
  *value_out = operator->();
  return jewels::success;
}

template <jewels::meta::ImplicitLifetimeType Value>
constexpr jewels::BinaryOutcome Optional<Value>::value(jewels::Out<const Value*> value_out) const& noexcept
{
  if (!has_value())
  {
    return jewels::failure;
  }
  *value_out = operator->();
  return jewels::success;
}

template <jewels::meta::ImplicitLifetimeType Value>
constexpr jewels::BinaryOutcome
Optional<Value>::value(jewels::FactoryOut<Value> value_out) && noexcept(std::is_nothrow_move_constructible_v<Value>)
{
  if (!has_value())
  {
    return jewels::failure;
  }
  value_out->emplace(std::move(**this));
  return jewels::success;
}

template <jewels::meta::ImplicitLifetimeType Value>
constexpr jewels::BinaryOutcome Optional<Value>::value(jewels::FactoryOut<Value> value_out) const&& noexcept(
  std::is_nothrow_copy_constructible_v<Value>)
{
  if (!has_value())
  {
    return jewels::failure;
  }
  value_out->emplace(**this);
  return jewels::success;
}

template <jewels::meta::ImplicitLifetimeType Value>
template <class Other>
constexpr Value Optional<Value>::value_or(Other&& default_value) const
  noexcept(std::is_nothrow_copy_constructible_v<Value> && std::is_nothrow_constructible_v<Value, Other&&>)
{
  if (has_value())
  {
    return **this;
  }
  return std::forward<Other>(default_value);
}

template <jewels::meta::ImplicitLifetimeType Value>
constexpr void Optional<Value>::swap(Optional<Value>& other) noexcept
{
  std::swap(fields_, other.fields_);
}

template <jewels::meta::ImplicitLifetimeType Value>
constexpr void Optional<Value>::reset() noexcept
{
  if (has_value())
  {
    const auto span = as_writable_bytes(jewels::as_single_item_span(fields_));
    std::fill(span.begin(), span.end(), std::byte{});
  }
}

template <jewels::meta::ImplicitLifetimeType Value>
template <class... Args>
constexpr Value& Optional<Value>::emplace(Args&&... args) noexcept(std::is_nothrow_constructible_v<Value, Args...>)
{
  jewels::memory::ObjectPolicy<Value>::construct(fields_.storage, std::forward<Args>(args)...);
  fields_.has_value = true;
  return jewels::memory::ObjectPolicy<Value>::get(fields_.storage);
}

template <jewels::meta::ImplicitLifetimeType Value>
constexpr bool Optional<Value>::operator==(const Optional<Value>& other) const
  noexcept(noexcept(std::declval<const Value&>() == std::declval<const Value&>()))
{
  if (has_value() == other.has_value())
  {
    return !has_value() || **this == *other;
  }
  return false;
}

template <jewels::meta::ImplicitLifetimeType Value>
constexpr bool Optional<Value>::operator==(const Value& other) const
  noexcept(noexcept(std::declval<const Value&>() == std::declval<const Value&>()))
{
  return has_value() && **this == other;
}

template <jewels::meta::ImplicitLifetimeType Value>
constexpr bool Optional<Value>::operator==(std::nullopt_t /*nullopt*/) const
{
  return !has_value();
}

template <jewels::meta::ImplicitLifetimeType Value>
void Optional<Value>::check_bad_access() const
{
  if (!has_value())
  {
    throw std::bad_optional_access{};
  }
}

template <class Value>
  requires jewels::meta::ImplicitLifetimeType<std::decay_t<Value>>
Optional<std::decay_t<Value>> make_optional(Value&& value)
{
  return Optional<std::decay_t<Value>>(std::forward<Value>(value));
}

template <jewels::meta::ImplicitLifetimeType Value, class... Args>
Optional<Value> make_optional(std::in_place_t /* in_place */, Args&&... args)
{
  return Optional<Value>(std::in_place, std::forward<Args>(args)...);
}

} // namespace jewels::tap
