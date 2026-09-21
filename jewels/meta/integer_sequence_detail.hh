// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

// IWYU pragma: private, include "jewels/meta/integer_sequence.hh"
#pragma once

#include <cstddef>
#include <utility>

namespace jewels::meta::detail
{

template <
  typename IntType,
  typename = std::integer_sequence<IntType>,
  typename = std::integer_sequence<IntType>,
  typename = std::integer_sequence<IntType>,
  typename = std::integer_sequence<IntType>,
  typename = std::integer_sequence<IntType>,
  typename = std::integer_sequence<IntType>,
  typename = std::integer_sequence<IntType>,
  typename = std::integer_sequence<IntType>,
  typename = std::integer_sequence<IntType>,
  typename = std::integer_sequence<IntType>,
  typename... Tail>
struct IntegerSequenceFlatten
{
};

template <
  typename IntType,
  IntType... seq00,
  IntType... seq01,
  IntType... seq02,
  IntType... seq03,
  IntType... seq04,
  IntType... seq05,
  IntType... seq06,
  IntType... seq07,
  IntType... seq08,
  IntType... seq09>
struct IntegerSequenceFlatten<
  IntType,
  std::integer_sequence<IntType, seq00...>,
  std::integer_sequence<IntType, seq01...>,
  std::integer_sequence<IntType, seq02...>,
  std::integer_sequence<IntType, seq03...>,
  std::integer_sequence<IntType, seq04...>,
  std::integer_sequence<IntType, seq05...>,
  std::integer_sequence<IntType, seq06...>,
  std::integer_sequence<IntType, seq07...>,
  std::integer_sequence<IntType, seq08...>,
  std::integer_sequence<IntType, seq09...>>
{
  using type = std::integer_sequence<
    IntType,
    seq00...,
    seq01...,
    seq02...,
    seq03...,
    seq04...,
    seq05...,
    seq06...,
    seq07...,
    seq08...,
    seq09...>;
};

template <
  typename IntType,
  typename Seq00,
  typename Seq01,
  typename Seq02,
  typename Seq03,
  typename Seq04,
  typename Seq05,
  typename Seq06,
  typename Seq07,
  typename Seq08,
  typename Seq09,
  typename Seq10,
  typename... Tail>
struct IntegerSequenceFlatten<
  IntType,
  Seq00,
  Seq01,
  Seq02,
  Seq03,
  Seq04,
  Seq05,
  Seq06,
  Seq07,
  Seq08,
  Seq09,
  Seq10,
  Tail...>
{
  using type = typename IntegerSequenceFlatten<
    IntType,
    Seq00,
    Seq01,
    Seq02,
    Seq03,
    Seq04,
    Seq05,
    Seq06,
    Seq07,
    Seq08,
    typename IntegerSequenceFlatten<IntType, Seq09, Seq10, Tail...>::type>::type;
};

template <typename IntType, IntType value, typename Seq>
struct MakeRepeatedIntegerSequence;

template <typename IntType, IntType value, std::size_t... i_n>
struct MakeRepeatedIntegerSequence<IntType, value, std::index_sequence<i_n...>>
{
  using type = std::integer_sequence<IntType, (static_cast<void>(i_n), value)...>;
};

} // namespace jewels::meta::detail
