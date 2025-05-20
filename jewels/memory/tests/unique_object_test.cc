// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/memory/unique_object.hh"

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <functional>
#include <utility>

namespace jewels::memory
{

struct Deleter
{
  template <class T>
  void operator()(T&& /*unused*/) noexcept // NOLINT(cppcoreguidelines-missing-std-forward)
  {
    ++deleted;
  }
  uint32_t deleted{0UL};
};

template <auto in_value>
struct Value
{
  static constexpr auto value{in_value};
};

struct Thrower
{
  Thrower() = default;
  Thrower(const Thrower&) = delete;
  Thrower(Thrower&&) noexcept(false) = delete;

  Thrower& operator=(const Thrower&) = delete;
  Thrower&& operator=(Thrower&&) noexcept(false) = delete;

  ~Thrower() noexcept(false);
};

TEST_CASE("Test unique object storage")
{
  STATIC_REQUIRE(sizeof(UniqueObject<int, Deleter>) == 12UL);
  STATIC_REQUIRE(sizeof(UniqueObject<int, Deleter, -1>) == 8UL);
}

TEST_CASE("Test noexcept")
{
  constexpr auto sentinel{-1};
  STATIC_REQUIRE((std::is_nothrow_move_constructible_v<UniqueObject<int, Deleter, sentinel>>));
  STATIC_REQUIRE((std::is_nothrow_move_assignable_v<UniqueObject<int, Deleter, sentinel>>));
  STATIC_REQUIRE((std::is_nothrow_destructible_v<UniqueObject<int, Deleter, sentinel>>));

  STATIC_REQUIRE((std::is_nothrow_move_constructible_v<UniqueObject<int, Deleter>>));
  STATIC_REQUIRE((std::is_nothrow_move_assignable_v<UniqueObject<int, Deleter>>));
  STATIC_REQUIRE((std::is_nothrow_destructible_v<UniqueObject<int, Deleter>>));

  STATIC_REQUIRE(!(std::is_nothrow_move_constructible_v<UniqueObject<Thrower, Deleter>>));
  STATIC_REQUIRE(!(std::is_nothrow_move_assignable_v<UniqueObject<Thrower, Deleter>>));
  STATIC_REQUIRE(!(std::is_nothrow_destructible_v<UniqueObject<Thrower, Deleter>>));
}

TEST_CASE("Deduction guide")
{
  STATIC_REQUIRE(std::is_same_v<decltype(UniqueObject{int{}, Deleter{}}), UniqueObject<int, Deleter>>);
}

TEMPLATE_TEST_CASE("Test unique object", "", (Value<-1>), (Value<std::nullopt>))
{
  constexpr auto sentinel{TestType::value};

  using DeleterRef = std::reference_wrapper<Deleter>;

  SECTION("Accessors")
  {
    UniqueObject<int, Deleter, sentinel> obj{8, Deleter{}};
    REQUIRE(obj);
    REQUIRE(obj.is_valid());
    REQUIRE(obj.get() == 8);
    REQUIRE(&*obj == &obj.get());
    REQUIRE(obj.operator->() == &obj.get());

    const auto& const_obj = obj;
    REQUIRE(const_obj);
    REQUIRE(const_obj.is_valid());
    REQUIRE(const_obj.get() == 8);
    REQUIRE(&*const_obj == &const_obj.get());
    REQUIRE(const_obj.operator->() == &const_obj.get());
  }

  SECTION("Destructor")
  {
    Deleter deleter{};
    {
      UniqueObject<int, DeleterRef, sentinel> obj{8, deleter};

      REQUIRE(obj);
      REQUIRE(obj.get() == 8);
      REQUIRE(deleter.deleted == 0UL);
    }
    REQUIRE(deleter.deleted == 1UL);
  }

  SECTION("From sentinel")
  {
    Deleter deleter{};
    {
      const UniqueObject<int, DeleterRef, sentinel> obj{std::nullopt, deleter};

      REQUIRE_FALSE(obj);
      REQUIRE(deleter.deleted == 0UL);
    }
    REQUIRE(deleter.deleted == 0UL);
  }

  SECTION("Manual release")
  {
    Deleter deleter{};
    {
      UniqueObject<int, DeleterRef, sentinel> obj{8, deleter};

      REQUIRE(obj);
      REQUIRE(obj.get() == 8);
      REQUIRE(deleter.deleted == 0UL);
      obj.release();
      REQUIRE(deleter.deleted == 1UL);
      obj.release();
      REQUIRE(deleter.deleted == 1UL);
    }
    REQUIRE(deleter.deleted == 1UL);
  }

  SECTION("Move constructor")
  {
    Deleter deleter{};
    {
      UniqueObject<int, DeleterRef, sentinel> obj_a{8, deleter};
      REQUIRE(obj_a);
      REQUIRE(obj_a.get() == 8);
      REQUIRE(deleter.deleted == 0UL);
      {
        auto obj_b{std::move(obj_a)};
        REQUIRE(obj_b);
        REQUIRE(obj_b.get() == 8);
        REQUIRE(deleter.deleted == 0UL);
      }
      REQUIRE(deleter.deleted == 1UL);
    }
    REQUIRE(deleter.deleted == 1UL);
  }

  SECTION("Move assignment")
  {
    Deleter deleter{};
    {
      UniqueObject<int, DeleterRef, sentinel> obj_a{8, deleter};
      REQUIRE(obj_a);
      REQUIRE(obj_a.get() == 8);
      REQUIRE(deleter.deleted == 0UL);
      {
        auto obj_b{std::move(obj_a)};
        REQUIRE(obj_b);
        REQUIRE(obj_b.get() == 8);
        REQUIRE(deleter.deleted == 0UL);
      }
      REQUIRE(deleter.deleted == 1UL);
    }
    REQUIRE(deleter.deleted == 1UL);
  }
}

} // namespace jewels::memory
