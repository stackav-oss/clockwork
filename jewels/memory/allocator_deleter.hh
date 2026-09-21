// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>
#include <memory_resource>
#include <type_traits>

namespace jewels::memory
{

namespace detail
{

/// Stores an allocator instance used by deleter specializations.
/// Provides a common allocator-carrying base with EBO-friendly storage.
/// @tparam Alloc Allocator type whose instance is stored in the deleter base.
template <typename Alloc>
// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions) move and copy construction perform same operation
class BaseDeleter
{
public:
  BaseDeleter() = default;
  BaseDeleter(const BaseDeleter& other) = default;

  /// Copy assignment is explicitly deleted so that it is not enabled through copy construction and move assignment.
  BaseDeleter& operator=(const BaseDeleter& other) = delete;

  /// Move assignment operator cannot be defaulted because std::pmr::polymorphic_allocator is not move assignable.
  /// So instead we manually destroy and copy construct the deleter.
  constexpr BaseDeleter& operator=(BaseDeleter&& other) noexcept(std::is_nothrow_move_constructible_v<BaseDeleter>);

  /// @param[in] alloc Allocator instance to store in the deleter base.
  constexpr explicit BaseDeleter(const Alloc& alloc);

  /// @returns A copy of the stored allocator instance.
  [[nodiscard]] constexpr Alloc get_allocator() const;

private:
  [[no_unique_address]] Alloc alloc_;
};

/// Alias for an allocator pointer type.
/// @tparam Alloc Allocator type from which to extract pointer.
template <typename Alloc>
using pointer_t = typename std::allocator_traits<Alloc>::pointer;

/// Alias for an allocator value type.
/// @tparam Alloc Allocator type from which to extract value_type.
template <typename Alloc>
using value_t = typename std::allocator_traits<Alloc>::value_type;

/// Implementation detail for SFINAE-based pointer adjustment detection.
/// Used internally to obtain a constant expression reference to a value without requiring instantiation.
/// @note This is not intended for direct user usage.
/// @warning Instantiations of this variable will fail to compile if T lacks external linkage.
template <typename T>
extern const T declexpr_v;

/// Implementation detail detecting pointer adjustment between derived and base class conversions.
/// Evaluates to `true` if casting a derived pointer to a base pointer involves an address adjustment,
/// which would indicate pointer incompatibility for polymorphic deleter conversions.
/// @tparam Derived Source type in the polymorphic hierarchy.
/// @tparam Base Target type in the polymorphic hierarchy.
/// @note This is not intended for direct user usage; used by is_polymorphic_convertible_v.
template <
  typename Derived,
  typename Base,
  std::enable_if_t<std::is_convertible_v<Derived*, Base*> && std::is_base_of_v<Base, Derived>, int> = 0>
inline constexpr bool has_pointer_adjustment_v =
// -Wundefined-var-template is suppressed here because declexpr_v<Derived> is intentionally undefined. It serves a
// similar purpose to std::declval<Derived>(), but for use in constant expressions instead of unevaluated contexts.
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wundefined-var-template"
#endif
  static_cast<const void*>(std::addressof(declexpr_v<Derived>)) !=
  static_cast<const Base*>(std::addressof(declexpr_v<Derived>));
#ifdef __clang__
#pragma clang diagnostic pop
#endif

/// SFINAE helper enabling conversions between compatible polymorphic deleters.
/// @tparam From Source allocator type.
/// @tparam To Destination allocator type.
template <typename From, typename To, typename = void>
inline constexpr bool is_polymorphic_convertible_v = false;

template <typename From, typename To>
inline constexpr bool
  is_polymorphic_convertible_v<From, To, std::void_t<decltype(has_pointer_adjustment_v<value_t<From>, value_t<To>>)>> =
    !std::is_same_v<From, To> && std::is_constructible_v<To, const From&> &&
    std::is_convertible_v<pointer_t<From>, pointer_t<To>> && std::is_base_of_v<value_t<To>, value_t<From>> &&
    std::has_virtual_destructor_v<value_t<To>> && !has_pointer_adjustment_v<value_t<From>, value_t<To>>;

} // namespace detail

/// Deleter for non-polymorphic allocations where pointer and value type are fixed.
/// Destroys and deallocates using the stored allocator; no cross-allocator conversion is allowed.
/// @tparam Alloc Allocator type used to destroy and deallocate the managed object.
template <typename Alloc>
class MonomorphicDeleter : public detail::BaseDeleter<Alloc>
{
  using AllocTraits = std::allocator_traits<Alloc>;

public:
  using pointer = typename AllocTraits::pointer;

  // expose constructor from const Alloc& to constructor overload set
  using detail::BaseDeleter<Alloc>::BaseDeleter;

  /// Disabled converting constructor declaration.
  /// @tparam OtherAlloc Any allocator type other than Alloc; this overload is intentionally deleted.
  /// @param[in] other Source deleter of an incompatible allocator type.
  // NOLINTNEXTLINE(modernize-use-constraints) nvcc compiles with C++17 which does not support concepts
  template <typename OtherAlloc, typename = std::enable_if_t<!std::is_same_v<OtherAlloc, Alloc>>>
  MonomorphicDeleter(const MonomorphicDeleter<OtherAlloc>& other) = delete;

  /// @param[in] ptr Pointer to the object to destroy and deallocate.
  /// @pre `ptr` must be a pointer to an object within its lifetime obtained from this deleter's allocator.
  constexpr void operator()(pointer ptr) const;
};

/// Exception type thrown when a polymorphic deleter detects a violation of its invariants, such as double deletion or
/// reset without reassigning deleter state.
class BadPolymorphicDeletion : public std::logic_error
{
public:
  explicit BadPolymorphicDeletion()
    : std::logic_error("Bad polymorphic deletion")
  {
  }
};

/// Deleter for polymorphic object graphs allocated via allocator-aware construction.
/// Supports safe converting construction across compatible allocator/value hierarchies,
/// and erases deletion details through a function pointer plus pointer offset.
/// @tparam Alloc Allocator type used for deallocation and erased-delete dispatch.
template <typename Alloc>
class PolymorphicDeleter : public detail::BaseDeleter<Alloc>
{
  using AllocTraits = std::allocator_traits<Alloc>;
  using Value = typename AllocTraits::value_type;
  using VoidAlloc = typename AllocTraits::template rebind_alloc<void>;
  using VoidPointer = typename AllocTraits::void_pointer;
  using Deleter = void (*)(const VoidAlloc&, VoidPointer);

public:
  using pointer = typename AllocTraits::pointer;

  // expose constructor from const Alloc& to constructor overload set
  using detail::BaseDeleter<Alloc>::BaseDeleter;

  /// Disable default constructor so a null unique_ptr with polymorphic deletion must explicitly initialize its deleter.
  PolymorphicDeleter() = delete;

  /// Converting constructor for compatible polymorphic deleters.
  /// @tparam OtherAlloc Source allocator type, convertible and compatible with Alloc.
  /// @param[in] other Source deleter from which erased delete state is copied.
  template <typename OtherAlloc, typename = std::enable_if_t<detail::is_polymorphic_convertible_v<OtherAlloc, Alloc>>>
  // NOLINTNEXTLINE(google-explicit-constructor) Allocators must be implicitly convertible
  constexpr PolymorphicDeleter(const PolymorphicDeleter<OtherAlloc>& other);

  /// Explicitly upgrades a monomorphic deleter to polymorphic deletion semantics.
  ///
  /// This constructor is `explicit` to prevent accidental implicit conversion in generic contexts
  /// (for example, `std::unique_ptr` converting constructors that depend on implicit deleter conversion).
  /// Callers should opt into this change intentionally, typically via `to_polymorphic(...)`.
  /// @param[in] other Source monomorphic deleter whose allocator state is reused.
  constexpr explicit PolymorphicDeleter(const MonomorphicDeleter<Alloc>& other);

  /// @returns The erased deleter function used by this deleter instance.
  [[nodiscard]] constexpr Deleter get_deleter() const;

  /// @param[in] ptr Pointer to the polymorphic object to delete.
  /// @pre The deleter must be in a valid state.
  /// @pre `ptr` must be a pointer to an object within its lifetime obtained from this deleter's allocator.
  /// @post The object pointed to by `ptr` is destroyed.
  /// @post The deleter is invalidated to detect potential double deletion or reset without reinitializing it.
  /// @throws BadPolymorphicDeletion if the deleter is in an invalid state.
  void operator()(pointer ptr);

private:
  /// @param[in] void_alloc Allocator rebound to void used for deallocation.
  /// @param[in] void_ptr Erased pointer to the storage being deallocated.
  static constexpr void do_delete(const VoidAlloc& void_alloc, VoidPointer void_ptr);

  Deleter deleter_{&do_delete};
};

namespace detail
{

/// Policy selecting monomorphic deleter behavior.
struct MonomorphicDeleterPolicy
{
  /// Maps an allocator to its monomorphic deleter type.
  /// @tparam Alloc Allocator type used to instantiate MonomorphicDeleter.
  template <typename Alloc>
  using deleter_type = MonomorphicDeleter<Alloc>;
};

/// Policy selecting polymorphic deleter behavior.
struct PolymorphicDeleterPolicy
{
  /// Maps an allocator to its polymorphic deleter type.
  /// @tparam Alloc Allocator type used to instantiate PolymorphicDeleter.
  template <typename Alloc>
  using deleter_type = PolymorphicDeleter<Alloc>;
};

/// Resolves the deleter type produced by a policy for a given allocator.
/// @tparam DeleterPolicy Type exposing template deleter_type<Alloc>.
/// @tparam Alloc Allocator type used by the resulting deleter.
template <typename DeleterPolicy, typename Alloc>
using BasicAllocatorDeleter = typename DeleterPolicy::template deleter_type<Alloc>;

/// Rebinds an allocator to a new value type.
/// @tparam Alloc Source allocator type.
/// @tparam T Target value type for allocator rebinding.
template <typename Alloc, typename T>
using RebindAlloc = typename std::allocator_traits<Alloc>::template rebind_alloc<T>;

/// Selects the internal deleter policy based on polymorphic-deletion mode.
/// @tparam enable_polymorphic_deletion True selects PolymorphicDeleterPolicy; false selects MonomorphicDeleterPolicy.
template <bool enable_polymorphic_deletion>
using PolymorphicDeleterIf =
  std::conditional_t<enable_polymorphic_deletion, PolymorphicDeleterPolicy, MonomorphicDeleterPolicy>;

} // namespace detail

/// Convenience alias for the allocator-backed deleter selected by the polymorphic-deletion flag.
/// @tparam T Object type associated with the allocator parameter.
/// @tparam enable_polymorphic_deletion True enables polymorphic deletion behavior.
/// @tparam Alloc Allocator type used by the deleter; defaults to std::allocator<void>.
template <typename T, bool enable_polymorphic_deletion = false, typename Alloc = std::allocator<void>>
using AllocatorDeleter = detail::
  BasicAllocatorDeleter<detail::PolymorphicDeleterIf<enable_polymorphic_deletion>, detail::RebindAlloc<Alloc, T>>;

/// PMR convenience alias of AllocatorDeleter using std::pmr::polymorphic_allocator.
/// @tparam T Object type associated with std::pmr::polymorphic_allocator.
/// @tparam enable_polymorphic_deletion True enables polymorphic deletion behavior.
template <typename T, bool enable_polymorphic_deletion = false>
using PmrDeleter = AllocatorDeleter<T, enable_polymorphic_deletion, std::pmr::polymorphic_allocator<void>>;

} // namespace jewels::memory

#include "jewels/memory/allocator_deleter.inl"
