// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/meta/integer_sequence_detail.hh" // IWYU pragma: export

#include <array>
#include <cstddef>
#include <utility>

namespace jewels::meta
{

/// Flattens the list of std::integer_sequence IntType to a single std::index_sequence
///  IntegerSequenceFlatten<integer_sequence<char, 1>, integer_sequence<char, 2>> == integer_sequence<char, 1, 2>
template <typename IntType, typename... Seq>
using IntegerSequenceFlatten = typename detail::IntegerSequenceFlatten<IntType, Seq...>::type;

/// Flattens the list of std::index_sequence IntType to a single std::index_sequence
///  IndexSequenceFlatten<index_sequence<1>, index_sequence<2>> == index_sequence<1, 2>
template <typename... Seq>
using IndexSequenceFlatten = IntegerSequenceFlatten<std::size_t, Seq...>;

/// Make an `integer_sequence<IntType, value, value...>` with `value` repeated `count` times
template <typename IntType, std::size_t count, IntType value>
using MakeRepeatedIntegerSequence =
  typename detail::MakeRepeatedIntegerSequence<IntType, value, std::make_index_sequence<count>>::type;

/// Make a std::array<T, N> using the IntType integers in the sequence
template <typename IntType, IntType... seq>
constexpr auto to_array(std::integer_sequence<IntType, seq...> /*unused*/)
{
  return std::array<IntType, sizeof...(seq)>{{seq...}};
}

/// Get the element at the given position in the sequence
///  get<1>(index_sequence<4,6,8>) == 6
template <std::size_t index, typename IntType, IntType... i_n>
constexpr IntType get(std::integer_sequence<IntType, i_n...> seq)
{
  static_assert(index < std::integer_sequence<IntType, i_n...>::size());
  return to_array(seq)[index];
}

} // namespace jewels::meta
