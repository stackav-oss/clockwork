// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/meta/call.hh"
#include "jewels/meta/type_traits.hh"

#include <functional>
#include <optional>
#include <type_traits>

namespace jewels::memory
{

/// Think unique_ptr but for non-ptr types.
/// @tparam Object The value type to store.
/// @tparam Deleter A callable that deletes the object.
/// @tparam seninel If the type naturally has a state that can
/// represent deleted, then no need to wrap in a std::optional.
template <class Object, class Deleter, auto sentinel = std::nullopt>
class UniqueObject
{
  // The sentinel value must either be std::nullopt_t which means the
  // object will be stored in a std::optional.  Or it has to be a
  // value of the same type as `Object`.
  static_assert(
    std::is_same_v<std::decay_t<decltype(sentinel)>, std::nullopt_t> || std::is_same_v<decltype(sentinel), Object>);

  // Force Deleter noexcept qualifications.
  static_assert(std::is_nothrow_move_constructible_v<Deleter> && std::is_nothrow_destructible_v<Deleter>);

  // Underlying storage.  Either Object or std::optional<Object>.
  using ObjectStorage = jewels::meta::
    Call<jewels::meta::ConditionalT<std::is_same_v<decltype(sentinel), Object>>, Object, std::optional<Object>>;

  // Whether or not release is noexcept.
  static constexpr auto noexcept_release{noexcept(std::declval<Deleter>()(std::declval<Object>()))};

public:
  /// Construct from a object.
  template <class InDeleter>
  UniqueObject(Object&& object, InDeleter&& deleter);

  /// Construct as a null object.
  template <class InDeleter>
  UniqueObject(std::nullopt_t /*unused*/, InDeleter&& deleter);

  UniqueObject(const UniqueObject&) = delete;
  UniqueObject(UniqueObject&& other) noexcept(
    // NOLINTNEXTLINE(performance-noexcept-move-constructor) Qualified based on the object type.
    std::is_nothrow_move_constructible_v<ObjectStorage> && std::is_nothrow_destructible_v<ObjectStorage>);

  void operator=(const UniqueObject&) = delete;
  UniqueObject& operator=(UniqueObject&& other) noexcept(
    // NOLINTNEXTLINE(performance-noexcept-move-constructor) Qualified based on the object type.
    std::is_nothrow_move_assignable_v<ObjectStorage> && std::is_nothrow_destructible_v<ObjectStorage>);

  // NOLINTNEXTLINE(performance-noexcept-destructor) Qualified based on the object type.
  ~UniqueObject() noexcept(noexcept_release && std::is_nothrow_destructible_v<ObjectStorage>);

  /// Check if a valid object is being held.
  /// @{
  [[nodiscard]] bool is_valid() const noexcept;
  [[nodiscard]] explicit operator bool() const noexcept;
  /// @}

  /// Get a reference to the valid object.  Requires a valid object to exist.
  /// @{
  [[nodiscard]] Object& get() noexcept;
  [[nodiscard]] const Object& get() const noexcept;
  [[nodiscard]] Object& operator*() noexcept;
  [[nodiscard]] const Object& operator*() const noexcept;
  /// @}

  /// Get a pointer to the valid object.
  /// @{
  [[nodiscard]] Object* operator->() noexcept;
  [[nodiscard]] const Object* operator->() const noexcept;
  /// @}

  /// If a valid object is held, call the deleter.
  void release() noexcept(noexcept_release);

private:
  /// The object to store.
  ObjectStorage object_;
  /// The deleter.
  Deleter deleter_;
};

/// Deduction guide.
template <class Object, class Deleter>
UniqueObject(Object&&, Deleter&&) -> UniqueObject<std::decay_t<Object>, std::decay_t<Deleter>>;

} // namespace jewels::memory

#include "jewels/memory/unique_object.inl"
