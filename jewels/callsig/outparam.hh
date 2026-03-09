// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/memory/pointers.hh"

#include <optional>
#include <type_traits>

namespace jewels
{

/// Forward declarations
template <class T>
class Out;
template <class T>
class InOut;
template <class T>
class OptionalOut;
template <class T>
class OptionalInOut;
template <class T>
class MaybeOut;
template <class T>
class FactoryResult;

///
/// Type traits for output parameter types
/// Note - base classes don't get traits because they're not intended to be used.
///

template <typename T>
struct IsOutParamType : std::false_type
{
};

template <typename T>
struct IsOutParamType<Out<T>> : std::true_type
{
};

template <typename T>
struct IsOutParamType<InOut<T>> : std::true_type
{
};

template <typename T>
struct IsOutParamType<OptionalOut<T>> : std::true_type
{
};

template <typename T>
struct IsOutParamType<OptionalInOut<T>> : std::true_type
{
};

template <typename T>
struct IsOutParamType<MaybeOut<T>> : std::true_type
{
};

template <typename T>
constexpr bool is_out_param_type_v = IsOutParamType<T>::value;

template <typename T>
concept OutParamType = is_out_param_type_v<T>;

/// Wrapper for output parameters.
/// The purpose of this class is to clearly indicate that a function is expected to modify the object it is passed.
/// This base class is inherited by Out and InOut without any changes.
template <class T>
class OutBase
{
public:
  /// Construct from a reference to an object
  explicit OutBase(T& ref) noexcept
    : ref_{jewels::memory::make_non_null_from_ref(ref)}
  {
  }

  ~OutBase() = default;

  // Prevent copy/move to avoid confusion
  OutBase(const OutBase&) = delete;
  OutBase& operator=(const OutBase&) = delete;
  OutBase(OutBase&&) = delete;
  OutBase& operator=(OutBase&&) = delete;

  /// Access the referenced object by pointer
  [[nodiscard]] T* operator->() noexcept
  {
    return ref_.get();
  }

  /// Access the referenced object by pointer (const)
  [[nodiscard]] const T* operator->() const noexcept
  {
    return ref_.get();
  }

  /// Access the referenced object by reference
  [[nodiscard]] T& operator*() noexcept
  {
    return *ref_;
  }

  /// Access the referenced object by reference (const)
  [[nodiscard]] const T& operator*() const noexcept
  {
    return *ref_;
  }

  /// Get the underlying pointer
  [[nodiscard]] T* get() noexcept
  {
    return ref_.get();
  }

  /// Get the underlying pointer (const)
  [[nodiscard]] const T* get() const noexcept
  {
    return ref_.get();
  }

private:
  jewels::memory::ObjectPtr<T> ref_;
};

/// Wrapper for output parameters.
/// The purpose of this class is to clearly indicate that a function is expected to modify the object it is passed.
template <class T>
class Out : public OutBase<T>
{
public:
  using OutBase<T>::OutBase;
};

/// Wrapper for parameters which are both inputs and outputs.
/// The purpose of this class is to clearly indicate that a function is expected to modify the object it is passed after
/// reading its value.
/// If that function can fail, the function's documentation should specify whether the output is modified in the case of
/// failure.
template <class T>
class InOut : public OutBase<T>
{
public:
  using OutBase<T>::OutBase;
};

/// Wrapper for caller-optional output parameters.
/// The caller decides whether an output is needed by passing either an object or nullopt.
/// This base class is inherited by OptionalOut and OptionalInOut without any changes.
template <class T>
class OptionalOutBase
{
public:
  /// Construct with a reference to an object (output is desired)
  explicit constexpr OptionalOutBase(T& ref) noexcept
    : ptr_{&ref}
  {
  }

  /// Construct with nullopt (output is not needed)
  constexpr OptionalOutBase( // NOLINT(google-explicit-constructor) We want to allow implicit conversion from nullopt
    std::nullopt_t /*nullopt*/) noexcept
    : ptr_{nullptr}
  {
  }

  /// Construct from another OptionalOutBase.
  // NOLINTNEXTLINE(google-explicit-constructor) We explicitly want to disable non-conversion implicit copying here.
  explicit OptionalOutBase(OptionalOutBase<T>& other) noexcept = default;

  /// Construct with an optional, either containing the object or nullopt (output depends on contents).
  explicit constexpr OptionalOutBase(std::optional<T>& maybe_ref) noexcept
    : ptr_(maybe_ref.has_value() ? &(*maybe_ref) : nullptr)
  {
  }

  ~OptionalOutBase() = default;

  // Prevent copying/moving to avoid confusion
  OptionalOutBase(const OptionalOutBase&) = delete;
  OptionalOutBase& operator=(const OptionalOutBase&) = delete;
  OptionalOutBase(OptionalOutBase&&) = delete;
  OptionalOutBase& operator=(OptionalOutBase&&) = delete;

  /// Check if output is desired (i.e., not nullopt)
  [[nodiscard]] constexpr bool has_value() const noexcept
  {
    return ptr_ != nullptr;
  }

  /// Check if output is desired (i.e., not nullopt)
  [[nodiscard]] constexpr explicit operator bool() const noexcept
  {
    return has_value();
  }

  /// (UNCHECKED) Access the referenced object by pointer
  /// @pre has_value() must be true
  [[nodiscard]] constexpr T* operator->() noexcept
  {
    return ptr_;
  }

  /// (UNCHECKED) Access the referenced object by pointer (const)
  /// @pre has_value() must be true
  [[nodiscard]] constexpr const T* operator->() const noexcept
  {
    return ptr_;
  }

  /// (UNCHECKED) Access the referenced object by reference
  /// @pre has_value() must be true
  [[nodiscard]] constexpr T& operator*() noexcept
  {
    return *ptr_;
  }

  /// (UNCHECKED) Access the referenced object by reference (const)
  /// @pre has_value() must be true
  [[nodiscard]] constexpr const T& operator*() const noexcept
  {
    return *ptr_;
  }

  /// Get the underlying pointer (may be null)
  [[nodiscard]] constexpr T* get() noexcept
  {
    return ptr_;
  }

  /// Get the underlying pointer (may be null)
  [[nodiscard]] constexpr const T* get() const noexcept
  {
    return ptr_;
  }

private:
  T* ptr_;
};

/// Wrapper for caller-optional output parameters.
/// The caller decides whether an output is needed by passing either an object or nullopt.
template <class T>
// Member functions are fully specified in the base class and explicitly inherited.
// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
class OptionalOut : public OptionalOutBase<T>
{
public:
  using OptionalOutBase<T>::OptionalOutBase;

  /// Construct from another OptionalOut
  // NOLINTNEXTLINE(google-explicit-constructor) We explicitly want to disable non-conversion implicit copying here.
  explicit OptionalOut(OptionalOut<T>& other) noexcept = default;
};

/// Wrapper for caller-optional input/output parameters.
/// The caller decides whether an output is needed by passing either an object or nullopt.
/// If that function can fail, the function's documentation should specify whether the output is modified in the case of
/// failure.
template <class T>
// Member functions are fully specified in the base class and explicitly inherited.
// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
class OptionalInOut : public OptionalOutBase<T>
{
public:
  using OptionalOutBase<T>::OptionalOutBase;

  /// Construct from another OptionalInOut
  // NOLINTNEXTLINE(google-explicit-constructor) We explicitly want to disable non-conversion implicit copying here.
  explicit OptionalInOut(OptionalInOut<T>& other) noexcept = default;
};

/// Wrapper for callee-optional output parameters.
/// The caller always provides an object, but the callee decides whether to modify it.
/// Note - a MaybeInOut version of this class doesn't make sense because the callee has no way of indicating whether an
/// input should be provided.
template <class T>
class MaybeOut
{
public:
  /// Construct from a reference to an object
  explicit MaybeOut(T& ref) noexcept
    : ptr_{jewels::memory::make_non_null_from_ref(ref)}
  {
  }

  ~MaybeOut() = default;

  // Prevent copy/move to avoid confusion
  MaybeOut(const MaybeOut&) = delete;
  MaybeOut& operator=(const MaybeOut&) = delete;
  MaybeOut(MaybeOut&&) = delete;
  MaybeOut& operator=(MaybeOut&&) = delete;

  /// Access the referenced object by pointer
  /// As a side effect, this will mark the output as valid.
  [[nodiscard]] T* operator->() noexcept
  {
    is_valid_ = true;
    return ptr_.get();
  }

  /// Access the referenced object by reference
  /// As a side effect, this will mark the output as valid.
  [[nodiscard]] T& operator*() noexcept
  {
    is_valid_ = true;
    return *ptr_;
  }

  /// Access the referenced object by pointer (const)
  /// Const version does not auto-mark as valid
  [[nodiscard]] const T* operator->() const noexcept
  {
    return ptr_.get();
  }

  /// Access the referenced object by reference (const)
  /// Const version does not auto-mark as valid
  [[nodiscard]] const T& operator*() const noexcept
  {
    return *ptr_;
  }

  /// Explicitly mark the output as valid
  void mark_valid() noexcept
  {
    is_valid_ = true;
  }

  /// Explicitly mark the output as invalid
  void mark_invalid() noexcept
  {
    is_valid_ = false;
  }

  /// Check if the output is valid
  [[nodiscard]] bool is_valid() const noexcept
  {
    return is_valid_;
  }

  /// Get the underlying pointer
  /// This does not mark the output as valid.
  [[nodiscard]] T* get() noexcept
  {
    return ptr_.get();
  }

  /// Get the underlying pointer (const)
  /// This does not mark the output as valid.
  [[nodiscard]] const T* get() const noexcept
  {
    return ptr_.get();
  }

private:
  memory::ObjectPtr<T> ptr_;
  bool is_valid_{false};
};

/// Convenience alias for declaring a factory output parameter.
/// This is the type you should use when declaring the function.
template <typename T>
using FactoryOut = Out<FactoryResult<T>>;

/// Wrapper for output parameters that are not default constructible.
///
/// This is intended for use with factory functions that construct objects via
/// private constructors, or other situations where a non-default-constructible
/// type is needed as an output parameter.
///
/// This is the type you should use at the caller (see examples in README.md).
template <class T>
class FactoryResult
{
public:
  /// Default construct as empty output
  FactoryResult() noexcept = default;

  ~FactoryResult() = default;

  // Prevent copy/move to avoid confusion (the value can still be copied/moved)
  FactoryResult(const FactoryResult&) = delete;
  FactoryResult& operator=(const FactoryResult&) = delete;
  FactoryResult(FactoryResult&&) = delete;
  FactoryResult& operator=(FactoryResult&&) = delete;

  /// Check if a value has been constructed
  [[nodiscard]] bool has_value() const noexcept
  {
    return storage_.has_value();
  }

  /// Check if a value has been constructed
  [[nodiscard]] explicit operator bool() const noexcept
  {
    return has_value();
  }

  /// (UNCHECKED) Access the constructed object by pointer
  /// @pre has_value() must be true
  [[nodiscard]] T* operator->() noexcept
  {
    return &(*storage_); // NOLINT(bugprone-unchecked-optional-access) Documented unchecked precondition
  }

  /// (UNCHECKED) Access the constructed object by pointer (const)
  /// @pre has_value() must be true
  [[nodiscard]] const T* operator->() const noexcept
  {
    return &(*storage_); // NOLINT(bugprone-unchecked-optional-access) Documented unchecked precondition
  }

  /// (UNCHECKED) Access the constructed object by reference
  /// @pre has_value() must be true
  [[nodiscard]] T& operator*() noexcept
  {
    return *storage_; // NOLINT(bugprone-unchecked-optional-access) Documented unchecked precondition
  }

  /// (UNCHECKED) Access the constructed object by reference (const)
  /// @pre has_value() must be true
  [[nodiscard]] const T& operator*() const noexcept
  {
    return *storage_; // NOLINT(bugprone-unchecked-optional-access) Documented unchecked precondition
  }

  /// Get pointer to the constructed object (may be null)
  [[nodiscard]] T* get() noexcept
  {
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access) It is checked by has_value()
    return has_value() ? &(*storage_) : nullptr;
  }

  /// Get const pointer to the constructed object (may be null)
  [[nodiscard]] const T* get() const noexcept
  {
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access) It is checked by has_value()
    return has_value() ? &(*storage_) : nullptr;
  }

  /// Construct the object in-place with the given arguments
  /// This is the primary way the caller would produce the output.
  template <typename... Args>
  T& emplace(Args&&... args)
  {
    return storage_.emplace(std::forward<Args>(args)...);
  }

  /// Assign to the contained object (or construct it if not already present)
  ///
  /// This method is an alternative to emplace and functions like the similar
  /// operator= in std::optional. It does *not* assign the FactoryResult itself,
  /// but rather to the contained object. As with std::optional, this is valid
  /// whether there is a contained value or not, and will either initialize a
  /// new value or assign to the existing one.
  ///
  /// @note: For POD struct types, this will be a more convenient way to
  /// construct than emplace would be, allowing the use of designated
  /// initializers directly:
  ///
  /// result = { .foo = 42, .bar = 3.14 };
  /// vs.
  /// result.emplace(Foo{.foo = 42, .bar = 3.14});
  ///
  /// Both of these do the same thing under the hood (and in most cases the
  /// temporary will be optimized out), so it's a readability difference.
  /// However, it is definitely less efficient to separately construct an object
  /// and then assign it than to emplace it directly; the temporary in the
  /// example above will be optimized out, but if you construct and then assign,
  /// it often will not be.
  template <typename U = std::remove_cv_t<T>>
  FactoryResult& operator=(U&& value)
  {
    storage_ = std::forward<U>(value);
    return *this;
  }

  /// Reset the value container (destroy any existing object)
  void reset() noexcept
  {
    storage_.reset();
  }

private:
  std::optional<T> storage_;
};

///
/// Deduction guides
/// Note - base classes don't get deduction guides because they're not intended to be used.
///

/// Deduction guide for Out
template <typename T>
Out(T&) -> Out<T>;

/// Deduction guide for InOut
template <typename T>
InOut(T&) -> InOut<T>;

/// Deduction guide for OptionalOut
template <typename T>
OptionalOut(T&) -> OptionalOut<T>;

/// Deduction guide for OptionalInOut
template <typename T>
OptionalInOut(T&) -> OptionalInOut<T>;

/// Deduction guide for MaybeOut
template <typename T>
MaybeOut(T&) -> MaybeOut<T>;

/// Deduction guide for FactoryResult
template <typename T>
FactoryResult(T&) -> FactoryResult<T>;

} // namespace jewels
