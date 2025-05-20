// IWYU pragma: private, include "jewels/memory/unique_object.hh"

#pragma once

#include "jewels/memory/unique_object.hh"

#include <optional>
#include <type_traits>
#include <utility>

namespace jewels::memory
{

namespace detail
{

template <class Object>
decltype(auto) unwrap_object(Object& object)
{
  return object;
}

template <class Object>
decltype(auto) unwrap_object(std::optional<Object>& object)
{
  // Users are required to check validity.
  return *object; // NOLINT(bugprone-unchecked-optional-access)
}

template <class Object>
decltype(auto) unwrap_object(const std::optional<Object>& object)
{
  // Users are required to check validity.
  return *object; // NOLINT(bugprone-unchecked-optional-access)
}

} // namespace detail

template <class Object, class Deleter, auto sentinel>
template <class InDeleter>
UniqueObject<Object, Deleter, sentinel>::UniqueObject(Object&& object, InDeleter&& deleter)
  : object_{std::move(object)}, deleter_{std::forward<InDeleter>(deleter)}
{
}

template <class Object, class Deleter, auto sentinel>
template <class InDeleter>
UniqueObject<Object, Deleter, sentinel>::UniqueObject(std::nullopt_t /*unused*/, InDeleter&& deleter)
  : object_{sentinel}, deleter_{std::forward<InDeleter>(deleter)}
{
}

template <class Object, class Deleter, auto sentinel>
UniqueObject<Object, Deleter, sentinel>::UniqueObject(UniqueObject&& other)
  // NOLINTNEXTLINE(performance-noexcept-move-constructor) Qualified based on the object type.
  noexcept(std::is_nothrow_move_constructible_v<ObjectStorage> && std::is_nothrow_destructible_v<ObjectStorage>)
  : object_{std::exchange(other.object_, sentinel)}, deleter_{std::move(other.deleter_)}
{
}

template <class Object, class Deleter, auto sentinel>
UniqueObject<Object, Deleter, sentinel>::~UniqueObject() noexcept(
  // NOLINTNEXTLINE(performance-noexcept-destructor) Qualified based on the object type.
  noexcept_release && std::is_nothrow_destructible_v<ObjectStorage>)
{
  release();
}

template <class Object, class Deleter, auto sentinel>
UniqueObject<Object, Deleter, sentinel>& UniqueObject<Object, Deleter, sentinel>::operator=(UniqueObject&& other)
  // NOLINTNEXTLINE(performance-noexcept-move-constructor) Qualified based on the object type.
  noexcept(std::is_nothrow_move_assignable_v<ObjectStorage> && std::is_nothrow_destructible_v<ObjectStorage>)
{
  release();
  object_ = std::exchange(other.object_, sentinel);
  deleter_ = std::move(other.deleter_);
  return *this;
}

template <class Object, class Deleter, auto sentinel>
bool UniqueObject<Object, Deleter, sentinel>::is_valid() const noexcept
{
  return object_ != sentinel;
}

template <class Object, class Deleter, auto sentinel>
UniqueObject<Object, Deleter, sentinel>::operator bool() const noexcept
{
  return is_valid();
}

template <class Object, class Deleter, auto sentinel>
Object& UniqueObject<Object, Deleter, sentinel>::get() noexcept
{
  return detail::unwrap_object(object_);
}

template <class Object, class Deleter, auto sentinel>
const Object& UniqueObject<Object, Deleter, sentinel>::get() const noexcept
{
  return detail::unwrap_object(object_);
}

template <class Object, class Deleter, auto sentinel>
Object& UniqueObject<Object, Deleter, sentinel>::operator*() noexcept
{
  return get();
}

template <class Object, class Deleter, auto sentinel>
const Object& UniqueObject<Object, Deleter, sentinel>::operator*() const noexcept
{
  return get();
}

template <class Object, class Deleter, auto sentinel>
Object* UniqueObject<Object, Deleter, sentinel>::operator->() noexcept
{
  return &get();
}

template <class Object, class Deleter, auto sentinel>
const Object* UniqueObject<Object, Deleter, sentinel>::operator->() const noexcept
{
  return &get();
}

template <class Object, class Deleter, auto sentinel>
void UniqueObject<Object, Deleter, sentinel>::release() noexcept(noexcept_release)
{
  if (object_ != sentinel)
  {
    deleter_(get());
    object_ = sentinel;
  }
}

} // namespace jewels::memory
