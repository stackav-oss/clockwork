// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/meta/concepts.hh"
#include "jewels/std/expected.hh"

#include <catch2/catch_test_macros.hpp>

#include <type_traits>

namespace jewels::meta
{

struct TrivialType
{
};

static_assert(std::is_trivial_v<TrivialType>);
static_assert(std::is_trivially_constructible_v<TrivialType>);
static_assert(std::is_trivially_copy_constructible_v<TrivialType>);
static_assert(std::is_trivially_destructible_v<TrivialType>);

struct NotTriviallyConstructible
{
  NotTriviallyConstructible();
};

static_assert(!std::is_trivially_constructible_v<NotTriviallyConstructible>);
static_assert(std::is_trivially_copy_constructible_v<NotTriviallyConstructible>);
static_assert(std::is_trivially_destructible_v<NotTriviallyConstructible>);

struct NotTriviallyCopyConstructible
{
  NotTriviallyCopyConstructible() = default;
  NotTriviallyCopyConstructible(const NotTriviallyCopyConstructible& /*other*/);
  NotTriviallyCopyConstructible(NotTriviallyCopyConstructible&& /*other*/) = delete;
  ~NotTriviallyCopyConstructible() = default;

  void operator=(const NotTriviallyCopyConstructible&) = delete;
  void operator=(NotTriviallyCopyConstructible&&) = delete;
};

static_assert(std::is_trivially_constructible_v<NotTriviallyCopyConstructible>);
static_assert(!std::is_trivially_copy_constructible_v<NotTriviallyCopyConstructible>);
static_assert(std::is_trivially_destructible_v<NotTriviallyCopyConstructible>);

struct NotTriviallyDestructible
{
  NotTriviallyDestructible() = default;
  NotTriviallyDestructible(const NotTriviallyDestructible& /*other*/) = default;
  NotTriviallyDestructible(NotTriviallyDestructible&& /*other*/) = delete;
  ~NotTriviallyDestructible();

  void operator=(const NotTriviallyDestructible&) = delete;
  void operator=(NotTriviallyDestructible&&) = delete;
};

static_assert(!std::is_trivially_constructible_v<NotTriviallyDestructible>);
static_assert(!std::is_trivially_copy_constructible_v<NotTriviallyDestructible>);
static_assert(!std::is_trivially_destructible_v<NotTriviallyDestructible>);

TEST_CASE("ImplicitLifetimeType")
{
  STATIC_REQUIRE(ImplicitLifetimeType<TrivialType>);
  STATIC_REQUIRE(ImplicitLifetimeType<NotTriviallyConstructible>);
  STATIC_REQUIRE(ImplicitLifetimeType<NotTriviallyCopyConstructible>);
  STATIC_REQUIRE(!ImplicitLifetimeType<NotTriviallyDestructible>);

  // Qualifiers
  STATIC_REQUIRE(ImplicitLifetimeType<const TrivialType>);
  STATIC_REQUIRE(!ImplicitLifetimeType<TrivialType&>);
  STATIC_REQUIRE(!ImplicitLifetimeType<TrivialType&&>);
  STATIC_REQUIRE(!ImplicitLifetimeType<const TrivialType&>);
  STATIC_REQUIRE(!ImplicitLifetimeType<const TrivialType&&>);
}

TEST_CASE("Byte")
{
  STATIC_REQUIRE(Byte<std::byte>);
  STATIC_REQUIRE(Byte<const std::byte>);
  STATIC_REQUIRE(!Byte<std::byte&>);
  STATIC_REQUIRE(!Byte<const std::byte&>);
  STATIC_REQUIRE(!Byte<int>);
}

TEST_CASE("SameAS")
{
  STATIC_REQUIRE(SameAs<int, int>);
  STATIC_REQUIRE(!SameAs<int, int&>);
  STATIC_REQUIRE(!SameAs<int, const int>);
  STATIC_REQUIRE(!SameAs<int, bool>);
}

TEST_CASE("DecaysTo")
{
  STATIC_REQUIRE(DecaysTo<int&, int>);
  STATIC_REQUIRE(DecaysTo<const int&, int>);
  STATIC_REQUIRE(!DecaysTo<int, int&>);
  STATIC_REQUIRE(!DecaysTo<int, const int&>);
  STATIC_REQUIRE(!DecaysTo<float&, int>);
}

TEST_CASE("ConvertibleTo")
{
  STATIC_REQUIRE(ConvertibleTo<int, bool>);
  STATIC_REQUIRE(ConvertibleTo<bool, int>);
  STATIC_REQUIRE(!ConvertibleTo<int, TrivialType>);
  STATIC_REQUIRE(!ConvertibleTo<int, void>);
  STATIC_REQUIRE(!ConvertibleTo<void, int>);
}

TEST_CASE("CastsTo")
{
  STATIC_REQUIRE(CastsTo<int, bool>);
  STATIC_REQUIRE(CastsTo<bool, int>);
  STATIC_REQUIRE(!CastsTo<int, TrivialType>);
  STATIC_REQUIRE(CastsTo<int, void>);
  STATIC_REQUIRE(!CastsTo<void, int>);
  STATIC_REQUIRE(CastsTo<jewels::expected<void, jewels::MonoError>, bool>);
}

} // namespace jewels::meta
