// IWYU pragma: private, include "jewels/container/tap/var_array.hh"
#pragma once

#include "jewels/container/tap/var_array.hh"

#include "jewels/container/tap/constants.hh"
#include "jewels/memory/aligned_storage.hh"
#include "jewels/meta/concepts.hh"
#include "jewels/std/expected.hh"

#include <fmt10/format.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>

namespace jewels::tap
{

namespace detail
{

template <meta::ImplicitLifetimeType Value, size_t fixed_capacity>
constexpr size_t var_array_between_padding()
{
  static_assert(sizeof(memory::AlignedStorage<Value>) == sizeof(Value));
  static_assert(sizeof(std::array<memory::AlignedStorage<Value>, fixed_capacity>) == sizeof(Value) * fixed_capacity);

  constexpr size_t data_size = sizeof(Value) * fixed_capacity;
  constexpr size_t remainder = data_size % constants::size_alignment;
  constexpr size_t padding = remainder == 0UL ? 0UL : sizeof(size_t) - remainder;
  return padding;
}

template <meta::ImplicitLifetimeType Value, size_t fixed_capacity>
constexpr size_t var_array_trailing_padding()
{
  if constexpr (alignof(Value) > constants::size_alignment)
  {
    return alignof(Value) - constants::size_alignment;
  }
  return 0UL;
}

template <meta::ImplicitLifetimeType Value, auto capacity, class... Args, auto... indices>
  requires(sizeof...(Args) == sizeof...(indices))
constexpr std::array<memory::AlignedStorage<Value>, capacity>
initialize(std::index_sequence<indices...> /*indices*/, Args&&... args)
{
  std::array<memory::AlignedStorage<Value>, capacity> storage{};
  (memory::ObjectPolicy<Value>::construct(storage[indices], std::forward<Args>(args)), ...);
  return storage;
}

} // namespace detail

template <meta::ImplicitLifetimeType Value, size_t fixed_capacity>
template <class First, class... Rest>
VarArray<Value, fixed_capacity>::VarArray(First&& first, Rest&&... rest)
  requires(!std::is_same_v<std::decay_t<First>, VarArray<Value, fixed_capacity>>)
  : fields_{
      .storage = detail::initialize<Value, fixed_capacity>(
        std::make_index_sequence<sizeof...(rest) + 1UL>{}, std::forward<First>(first), std::forward<Rest>(rest)...),
      .size{sizeof...(Rest) + 1UL}}
{
  static_assert((sizeof...(Rest) + 1UL) <= fixed_capacity);
}

template <class Derived, meta::ImplicitLifetimeType Value, size_t fixed_capacity>
auto VarArrayInterface<Derived, Value, fixed_capacity>::at(size_t index) -> reference
{
  if (index >= fields().size)
  {
    throw std::out_of_range{fmt::format(
      "VarArray<{}, {}>: index [{}] out of bounds [{}].", typeid(Value).name(), fixed_capacity, index, fields().size)};
  }
  return (*this)[index];
}

template <class Derived, meta::ImplicitLifetimeType Value, size_t fixed_capacity>
auto VarArrayInterface<Derived, Value, fixed_capacity>::at(size_t index) const -> const_reference
{
  if (index >= fields().size)
  {
    throw std::out_of_range{fmt::format(
      "VarArray<{}, {}>: index [{}] out of bounds [{}].", typeid(Value).name(), fixed_capacity, index, fields().size)};
  }
  return (*this)[index];
}

template <class Derived, meta::ImplicitLifetimeType Value, size_t fixed_capacity>
auto VarArrayInterface<Derived, Value, fixed_capacity>::operator[](size_t index) -> reference
{
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) operator[] is explicitly unchecked
  return memory::ObjectPolicy<Value>::get(fields().storage[index]);
}

template <class Derived, meta::ImplicitLifetimeType Value, size_t fixed_capacity>
auto VarArrayInterface<Derived, Value, fixed_capacity>::operator[](size_t index) const -> const_reference
{
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) operator[] is explicitly unchecked
  return memory::ObjectPolicy<Value>::get(fields().storage[index]);
}

template <class Derived, meta::ImplicitLifetimeType Value, size_t fixed_capacity>
auto VarArrayInterface<Derived, Value, fixed_capacity>::begin() -> iterator
{
  return std::begin(span());
}

template <class Derived, meta::ImplicitLifetimeType Value, size_t fixed_capacity>
auto VarArrayInterface<Derived, Value, fixed_capacity>::begin() const -> const_iterator
{
  return std::cbegin(span());
}

template <class Derived, meta::ImplicitLifetimeType Value, size_t fixed_capacity>
auto VarArrayInterface<Derived, Value, fixed_capacity>::end() -> iterator
{
  return std::end(span());
}

template <class Derived, meta::ImplicitLifetimeType Value, size_t fixed_capacity>
auto VarArrayInterface<Derived, Value, fixed_capacity>::end() const -> const_iterator
{
  return std::cend(span());
}

template <class Derived, meta::ImplicitLifetimeType Value, size_t fixed_capacity>
constexpr auto VarArrayInterface<Derived, Value, fixed_capacity>::size() const -> size_type
{
  return fields().size;
}

template <class Derived, meta::ImplicitLifetimeType Value, size_t fixed_capacity>
constexpr auto VarArrayInterface<Derived, Value, fixed_capacity>::capacity() const -> size_type
{
  return fixed_capacity;
}

template <class Derived, meta::ImplicitLifetimeType Value, size_t fixed_capacity>
auto VarArrayInterface<Derived, Value, fixed_capacity>::data() -> pointer
{
  return memory::ObjectPolicy<Value>::ptr(fields().storage[0]);
}

template <class Derived, meta::ImplicitLifetimeType Value, size_t fixed_capacity>
auto VarArrayInterface<Derived, Value, fixed_capacity>::data() const -> const_pointer
{
  return memory::ObjectPolicy<Value>::ptr(fields().storage[0]);
}

template <class Derived, meta::ImplicitLifetimeType Value, size_t fixed_capacity>
constexpr void VarArrayInterface<Derived, Value, fixed_capacity>::clear()
{
  wipe(std::exchange(fields().size, 0UL));
}

template <class Derived, meta::ImplicitLifetimeType Value, size_t fixed_capacity>
void VarArrayInterface<Derived, Value, fixed_capacity>::reserve(size_t new_capacity)
{
  if (new_capacity > capacity())
  {
    throw std::length_error{fmt::format(
      "VarArray<{}, {}>: reserve: new capacity [{}] is too large [{}].",
      typeid(Value).name(),
      fixed_capacity,
      new_capacity,
      capacity())};
  }
}

template <class Derived, meta::ImplicitLifetimeType Value, size_t fixed_capacity>
void VarArrayInterface<Derived, Value, fixed_capacity>::resize(size_t new_size)
{
  resize_impl(new_size);
}

template <class Derived, meta::ImplicitLifetimeType Value, size_t fixed_capacity>
void VarArrayInterface<Derived, Value, fixed_capacity>::resize(size_t new_size, const_reference new_value)
{
  resize_impl(new_size, new_value);
}

template <class Derived, meta::ImplicitLifetimeType Value, size_t fixed_capacity>
template <class... Args>
  requires(sizeof...(Args) <= 1 && (sizeof...(Args) == 0 || (std::is_same_v<Args, Value> && ...)))
void VarArrayInterface<Derived, Value, fixed_capacity>::resize_impl(size_t new_size, const Args&... args)
{
  if (new_size > capacity())
  {
    throw std::length_error{fmt::format(
      "VarArray<{}, {}>: resize: new capacity [{}] is too large [{}].",
      typeid(Value).name(),
      fixed_capacity,
      new_size,
      capacity())};
  }

  const auto old_size = std::exchange(fields().size, new_size);
  if (old_size < new_size)
  {
    for (auto index = old_size; index < new_size; ++index)
    {
      memory::ObjectPolicy<Value>::construct(fields().storage.at(index), args...);
    }
  }
  else
  {
    wipe(old_size - new_size);
  }
}

template <class Derived, meta::ImplicitLifetimeType Value, size_t fixed_capacity>
template <class... Args>
void VarArrayInterface<Derived, Value, fixed_capacity>::push_back(Args&&... args)
{
  if (!try_emplace_back(std::forward<Args>(args)...))
  {
    throw std::length_error{fmt::format(
      "VarArray<{}, {}>: push_back: Insufficient capacity [{}].", typeid(Value).name(), fixed_capacity, capacity())};
  }
}

template <class Derived, meta::ImplicitLifetimeType Value, size_t fixed_capacity>
template <class... Args>
auto VarArrayInterface<Derived, Value, fixed_capacity>::emplace_back(Args&&... args) -> reference
{
  auto result = try_emplace_back(std::forward<Args>(args)...);
  if (!result)
  {
    throw std::length_error{fmt::format(
      "VarArray<{}, {}>: emplace_back: Insufficient capacity [{}].", typeid(Value).name(), fixed_capacity, capacity())};
  }
  return **result;
}

template <class Derived, meta::ImplicitLifetimeType Value, size_t fixed_capacity>
void VarArrayInterface<Derived, Value, fixed_capacity>::pop_back()
{
  if (!try_pop_back())
  {
    throw std::length_error{
      fmt::format("VarArray<{}, {}>: pop_back: Container is empty.", typeid(Value).name(), fixed_capacity)};
  }
}

template <class Derived, meta::ImplicitLifetimeType Value, size_t fixed_capacity>
bool VarArrayInterface<Derived, Value, fixed_capacity>::try_pop_back()
{
  if (empty())
  {
    return false;
  }
  --fields().size;
  wipe(1UL);
  return true;
}

template <class Derived, meta::ImplicitLifetimeType Value, size_t fixed_capacity>
constexpr bool VarArrayInterface<Derived, Value, fixed_capacity>::full() const
{
  return fields().size == capacity();
}

template <class Derived, meta::ImplicitLifetimeType Value, size_t fixed_capacity>
constexpr bool VarArrayInterface<Derived, Value, fixed_capacity>::empty() const
{
  return fields().size == 0UL;
}

template <class Derived, meta::ImplicitLifetimeType Value, size_t fixed_capacity>
template <class... Args>
auto VarArrayInterface<Derived, Value, fixed_capacity>::try_emplace_back(Args&&... args)
  -> jewels::expected<iterator, jewels::MonoError>
{
  if (full())
  {
    return jewels::unexpected{jewels::MonoError{}};
  }

  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) full() check guarantees this is in bounds
  memory::ObjectPolicy<Value>::construct(fields().storage[fields().size], std::forward<Args>(args)...);
  ++fields().size;
  return std::prev(end());
}

template <class Derived, meta::ImplicitLifetimeType Value, size_t fixed_capacity>
auto VarArrayInterface<Derived, Value, fixed_capacity>::span() -> std::span<value_type>
{
  return std::span{data(), fields().size};
}

template <class Derived, meta::ImplicitLifetimeType Value, size_t fixed_capacity>
auto VarArrayInterface<Derived, Value, fixed_capacity>::span() const -> std::span<const value_type>
{
  return std::span{data(), fields().size};
}

template <class Derived, meta::ImplicitLifetimeType Value, size_t fixed_capacity>
void VarArrayInterface<Derived, Value, fixed_capacity>::wipe(size_t count)
{
  // No need to call destructors as the value type is trivially destructible.
  auto bytes = as_writable_bytes(std::span{fields().storage});
  auto bytes_to_wipe = bytes.subspan(fields().size * sizeof(Value), count * sizeof(Value));
  std::fill(bytes_to_wipe.begin(), bytes_to_wipe.end(), std::byte{0U});
}

template <class Derived, meta::ImplicitLifetimeType Value, size_t fixed_capacity>
constexpr auto& VarArrayInterface<Derived, Value, fixed_capacity>::fields()
{
  return static_cast<Derived&>(*this).fields_;
}

template <class Derived, meta::ImplicitLifetimeType Value, size_t fixed_capacity>
constexpr const auto& VarArrayInterface<Derived, Value, fixed_capacity>::fields() const
{
  return static_cast<const Derived&>(*this).fields_;
}

template <class Derived, meta::ImplicitLifetimeType Value, size_t fixed_capacity>
auto VarArrayInterface<Derived, Value, fixed_capacity>::erase(iterator pos) -> iterator
{
  const auto pos_index = std::distance(begin(), pos);
  std::move(std::next(pos), end(), pos);
  --fields().size;
  wipe(1UL);
  return std::next(begin(), pos_index);
}

template <class Derived, meta::ImplicitLifetimeType Value, size_t fixed_capacity>
auto VarArrayInterface<Derived, Value, fixed_capacity>::erase(const_iterator pos) -> iterator
{
  const auto pos_index = std::distance(begin(), pos);
  return erase(std::next(begin(), pos_index));
}

template <class Derived, meta::ImplicitLifetimeType Value, size_t fixed_capacity>
auto VarArrayInterface<Derived, Value, fixed_capacity>::erase(iterator first, iterator last) -> iterator
{
  const auto first_index = std::distance(begin(), first);
  // NB: This is the std::move from <algorithm> and not the more common one from <utility>.
  std::move(last, end(), first); // NOLINT(readability-suspicious-call-argument) Argument order is correct here
  const auto count{static_cast<uint64_t>(std::distance(first, last))};
  fields().size -= count;
  wipe(count);
  return std::next(begin(), first_index);
}

template <class Derived, meta::ImplicitLifetimeType Value, size_t fixed_capacity>
template <class NewValue>
auto VarArrayInterface<Derived, Value, fixed_capacity>::insert(iterator pos, NewValue&& new_value) -> iterator
{
  if (full())
  {
    throw std::length_error{fmt::format(
      "VarArray<{}, {}>: insert: inserting an element would exceed capacity [{}].",
      typeid(Value).name(),
      fixed_capacity,
      fixed_capacity)};
  }
  const auto pos_index = std::distance(begin(), pos);
  ++fields().size;
  auto new_pos = std::next(begin(), pos_index);
  std::move_backward(new_pos, std::prev(end()), end());
  *new_pos = std::forward<NewValue>(new_value);
  return new_pos;
}

template <class Derived, meta::ImplicitLifetimeType Value, size_t fixed_capacity>
template <class InputIter>
auto VarArrayInterface<Derived, Value, fixed_capacity>::insert(iterator pos, InputIter first, InputIter last)
  -> iterator
{
  const auto count = std::distance(first, last);
  if (size() + static_cast<size_t>(count) > fixed_capacity)
  {
    throw std::length_error{fmt::format(
      "VarArray<{}, {}>: insert: inserting an element would exceed capacity [{}].",
      typeid(Value).name(),
      fixed_capacity,
      fixed_capacity)};
  }
  return insert_impl(pos, first, last);
}

template <class Derived, meta::ImplicitLifetimeType Value, size_t fixed_capacity>
template <class InputIter>
auto VarArrayInterface<Derived, Value, fixed_capacity>::insert_impl(
  iterator pos, InputIter first, InputIter last) noexcept -> iterator
{
  const auto count = std::distance(first, last);
  const auto pos_index = std::distance(begin(), pos);
  fields().size += static_cast<size_t>(count);
  auto new_pos = std::next(begin(), pos_index);
  std::move_backward(new_pos, std::prev(end(), count), end());
  std::copy(first, last, new_pos);
  return new_pos;
}

template <class Derived, meta::ImplicitLifetimeType Value, size_t fixed_capacity>
constexpr bool VarArrayInterface<Derived, Value, fixed_capacity>::try_set(std::span<const value_type> other) noexcept
{
  if (other.size() > fixed_capacity)
  {
    return false;
  }
  const size_t old_size = std::exchange(fields().size, other.size());
  if (old_size > fields().size)
  {
    this->wipe(old_size - fields().size);
  }
  std::copy(other.begin(), other.end(), std::begin(*this));
  return true;
}

template <typename T, size_t fixed_capacity>
bool operator==(const VarArray<T, fixed_capacity>& lhs, const VarArray<T, fixed_capacity>& rhs)
{
  return std::equal(lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
}

template <typename T, size_t fixed_capacity>
bool operator!=(const VarArray<T, fixed_capacity>& lhs, const VarArray<T, fixed_capacity>& rhs)
{
  return !(lhs == rhs);
}

} // namespace jewels::tap
