// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/cpp_message_encoding.hh"
#include "clockwork/logging/cpp_schema_encoding.hh"

#include <type_traits> // IWYU pragma: keep

namespace clockwork
{

/// Fwd declaration for the Tap initialization struct.  Will be specialized for a schema tag.
template <class>
struct TapInit;

/// Fwd declaration for the Tachyon layout struct.  Will be specialized for a schema tag.
template <class>
struct Tachyon;

/// Fwd declaration for the Tap interface struct.  Will be specialized for a schema tag.
template <class>
struct Tap;

/// Alias for convenience.
template <class T>
using Tappy = Tap<Tachyon<T>>;

/// Alias for convenience.
template <class T>
using TappyInit = TapInit<Tachyon<T>>;

template <typename T>
struct IsTachyon : public std::false_type
{
};

template <typename T>
struct IsTachyon<Tachyon<T>> : public std::true_type
{
};

template <typename T>
concept TachyonType = IsTachyon<T>::value;

template <typename T>
struct IsTappy : public std::false_type
{
};

template <typename T>
struct IsTappy<Tap<Tachyon<T>>> : public std::true_type
{
};

template <typename T>
concept TappyType = IsTappy<T>::value;

template <typename T>
  requires(TachyonType<T> || TappyType<T>)
struct LoggingTraits
{
};

template <typename T>
struct LoggingTraits<Tap<Tachyon<T>>> : public LoggingTraits<Tachyon<T>>
{
};

struct TachyonLoggingTraits
{
  static constexpr auto has_metadata = true;
  static constexpr auto message_encoding = clockwork_logging::CppMessageEncoding::tachyon;
  static constexpr auto schema_encoding = clockwork_logging::CppSchemaEncoding::clockwork_tachyon;
};

namespace detail
{
/// Base case helper struct for removing Tap<Tachyon<...>>
template <class T>
struct RemoveTapTachyonImpl
{
  using Type = T;
};

/// Sepcialization for removing Tap<Tachyon<...>>
template <class T>
struct RemoveTapTachyonImpl<Tap<Tachyon<T>>>
{
  using Type = T;
};
} // namespace detail

/// Remove Tap<Tachyon<...>> from a type if exists.
template <class T>
using RemoveTapTachyon = typename detail::RemoveTapTachyonImpl<T>::Type;

} // namespace clockwork
