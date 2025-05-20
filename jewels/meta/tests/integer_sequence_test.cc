// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/meta/integer_sequence.hh"

#include <catch2/catch_test_macros.hpp>

#include <type_traits>

namespace jewels::meta
{

TEST_CASE("Test IntegerSequenceFlatten")
{
  static_assert(std::is_same_v<IntegerSequenceFlatten<std::size_t>, std::index_sequence<>>);
  static_assert(std::is_same_v<
                IntegerSequenceFlatten<char, std::integer_sequence<char, 'a'>, std::integer_sequence<char, 'z'>>,
                std::integer_sequence<char, 'a', 'z'>>);
}

TEST_CASE("Test IndexSequenceFlatten")
{
  using Seq00 = std::index_sequence<0>;
  using Seq01 = std::index_sequence<1>;
  using Seq02 = std::index_sequence<2>;
  using Seq03 = std::index_sequence<3>;
  using Seq04 = std::index_sequence<4>;
  using Seq05 = std::index_sequence<5>;
  using Seq06 = std::index_sequence<6>;
  using Seq07 = std::index_sequence<7>;
  using Seq08 = std::index_sequence<8>;
  using Seq09 = std::index_sequence<9>;
  using Seq10 = std::index_sequence<10>;
  using Seq11 = std::index_sequence<11>;
  using Seq12 = std::index_sequence<12>;
  using Seq00_05 = std::index_sequence<0, 1, 2, 3, 4, 5>;

  // Null cases
  static_assert(std::is_same_v<IndexSequenceFlatten<>, std::index_sequence<>>);
  static_assert(std::is_same_v<IndexSequenceFlatten<std::index_sequence<>>, std::index_sequence<>>);

  // Identity
  static_assert(std::is_same_v<IndexSequenceFlatten<Seq00_05>, Seq00_05>);

  // Removal of empty sequence
  static_assert(std::is_same_v<IndexSequenceFlatten<std::index_sequence<>, Seq00, Seq01>, std::index_sequence<0, 1>>);
  static_assert(std::is_same_v<IndexSequenceFlatten<Seq00, std::index_sequence<>, Seq01>, std::index_sequence<0, 1>>);
  static_assert(std::is_same_v<IndexSequenceFlatten<Seq00, Seq01, std::index_sequence<>>, std::index_sequence<0, 1>>);

  // Flattening near the 10 sequence recursion threshold
  static_assert(std::is_same_v<
                IndexSequenceFlatten<Seq00, Seq01, Seq02, Seq03, Seq04, Seq05, Seq06, Seq07, Seq08>,
                std::index_sequence<0, 1, 2, 3, 4, 5, 6, 7, 8>>);
  static_assert(std::is_same_v<
                IndexSequenceFlatten<Seq00, Seq01, Seq02, Seq03, Seq04, Seq05, Seq06, Seq07, Seq08, Seq09>,
                std::index_sequence<0, 1, 2, 3, 4, 5, 6, 7, 8, 9>>);
  static_assert(std::is_same_v<
                IndexSequenceFlatten<Seq00, Seq01, Seq02, Seq03, Seq04, Seq05, Seq06, Seq07, Seq08, Seq09, Seq10>,
                std::index_sequence<0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10>>);
  static_assert(
    std::is_same_v<
      IndexSequenceFlatten<Seq00, Seq01, Seq02, Seq03, Seq04, Seq05, Seq06, Seq07, Seq08, Seq09, Seq10, Seq11>,
      std::index_sequence<0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11>>);
  static_assert(
    std::is_same_v<
      IndexSequenceFlatten<Seq00, Seq01, Seq02, Seq03, Seq04, Seq05, Seq06, Seq07, Seq08, Seq09, Seq10, Seq11, Seq12>,
      std::index_sequence<0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12>>);

  // Sanity check for size>1 sequences
  static_assert(std::is_same_v<
                IndexSequenceFlatten<std::index_sequence<0, 2, 4>, std::index_sequence<1, 3, 5>>,
                std::index_sequence<0, 2, 4, 1, 3, 5>>);
  // Sanity check for flattening flattened sequences
  static_assert(
    std::is_same_v<
      IndexSequenceFlatten<IndexSequenceFlatten<Seq00, Seq02, Seq04>, IndexSequenceFlatten<Seq01, Seq03, Seq05>>,
      std::index_sequence<0, 2, 4, 1, 3, 5>>);
}

TEST_CASE("Test integer_sequence get")
{
  static_assert(get<0>(std::index_sequence<4>{}) == 4);
  static_assert(get<0>(std::index_sequence<9, 8, 7, 6>{}) == 9);
  static_assert(get<2>(std::index_sequence<9, 8, 7, 6>{}) == 7);
  static_assert(get<3>(std::index_sequence<9, 8, 7, 6>{}) == 6);
  static_assert(get<1>(std::integer_sequence<char, 'a', 'b', 'c'>{}) == 'b');
}

TEST_CASE("Test integer_sequence to_array")
{
  CHECK(to_array(std::index_sequence<>{}) == std::array<size_t, 0>{{}});
  CHECK(to_array(std::index_sequence<1, 2, 3, 4>{}) == std::array<size_t, 4>{{1, 2, 3, 4}});
  CHECK(to_array(std::integer_sequence<char>{}) == std::array<char, 0>{{}});
  CHECK(to_array(std::integer_sequence<char, 1>{}) == std::array<char, 1>{{1}});
}

} // namespace jewels::meta
