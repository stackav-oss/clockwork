// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/container/tap/optional.hh"
#include "jewels/std/span.hh"

#include <__stddef_offsetof.h>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <type_traits>

namespace jewels::tap
{

struct Sentinel
{
  static constexpr uint32_t value{std::numeric_limits<uint32_t>::max()};
};

class WrappedUInt32
{
public:
  explicit WrappedUInt32(uint32_t value)
    : value_{value}
  {
  }

  explicit WrappedUInt32(Sentinel /*sentinel*/)
    : value_{Sentinel::value}
  {
  }

  WrappedUInt32(const WrappedUInt32&) = default;
  WrappedUInt32(WrappedUInt32&&) = default;
  WrappedUInt32& operator=(const WrappedUInt32&) = default;
  WrappedUInt32& operator=(WrappedUInt32&&) = default;
  ~WrappedUInt32() = default;

  bool operator==(const WrappedUInt32&) const = default;

  WrappedUInt32& operator=(Sentinel /*sentinel*/)
  {
    value_ = Sentinel::value;
    return *this;
  }

private:
  uint32_t value_{};
};

constexpr auto check_zero = [](auto byte) { return byte == std::byte{}; };

using Opt = Optional<WrappedUInt32>;

TEST_CASE("Layout")
{
  STATIC_REQUIRE(alignof(bool) == constants::bool_alignment);

  STATIC_REQUIRE(detail::optional_trailing_padding<uint8_t>() == 0UL);
  STATIC_REQUIRE(detail::optional_trailing_padding<uint16_t>() == 1UL);
  STATIC_REQUIRE(detail::optional_trailing_padding<uint32_t>() == 3UL);
  STATIC_REQUIRE(detail::optional_trailing_padding<uint64_t>() == 7UL);

  SECTION("No padding")
  {
    STATIC_REQUIRE(alignof(detail::OptionalLayout<uint8_t>) == 1UL);
    STATIC_REQUIRE(offsetof(detail::OptionalLayout<uint8_t>, storage) == 0UL);
    STATIC_REQUIRE(sizeof(std::declval<detail::OptionalLayout<uint8_t>>().storage) == 1UL);
    STATIC_REQUIRE(offsetof(detail::OptionalLayout<uint8_t>, has_value) == 1UL);
    STATIC_REQUIRE(sizeof(std::declval<detail::OptionalLayout<uint8_t>>().has_value) == 1UL);
    STATIC_REQUIRE(sizeof(detail::OptionalLayout<uint8_t>) == 2UL);
  }

  SECTION("Padding")
  {
    STATIC_REQUIRE(alignof(detail::OptionalLayout<uint32_t>) == 4UL);
    STATIC_REQUIRE(offsetof(detail::OptionalLayout<uint32_t>, storage) == 0UL);
    STATIC_REQUIRE(sizeof(std::declval<detail::OptionalLayout<uint32_t>>().storage) == 4UL);
    STATIC_REQUIRE(offsetof(detail::OptionalLayout<uint32_t>, has_value) == 4UL);
    STATIC_REQUIRE(sizeof(std::declval<detail::OptionalLayout<uint32_t>>().has_value) == 1UL);
    STATIC_REQUIRE(offsetof(detail::OptionalLayout<uint32_t>, padding) == 5UL);
    STATIC_REQUIRE(sizeof(std::declval<detail::OptionalLayout<uint32_t>>().padding) == 3UL);
    STATIC_REQUIRE(sizeof(detail::OptionalLayout<uint32_t>) == 8UL);
  }
}

TEST_CASE("is_optional")
{
  STATIC_REQUIRE(detail::is_optional_v<Optional<uint32_t>>);
  STATIC_REQUIRE(!detail::is_optional_v<uint32_t>);
}

// We have to move even though it doesn't benefit to make sure the
// APIs work as expected for both l-values and r-values.  Otherwise
// generic code might not behave correctly.

TEST_CASE("Construction")
{
  SECTION("Default")
  {
    Opt opt{};
    REQUIRE(std::ranges::all_of(as_bytes(jewels::as_single_item_span(opt)), check_zero));
    REQUIRE(!opt);
  }
  SECTION("From std::nullopt")
  {
    Opt opt{std::nullopt};
    REQUIRE(std::ranges::all_of(as_bytes(jewels::as_single_item_span(opt)), check_zero));
  }
  SECTION("Optional of differnet type")
  {
    STATIC_REQUIRE(std::is_constructible_v<WrappedUInt32, Sentinel>);
    SECTION("Invalid")
    {
      Optional<Sentinel> other{};
      REQUIRE(!Opt{other});
      // NOLINTNEXTLINE(performance-move-const-arg) Intentionally testing move behavior
      REQUIRE(!Opt{std::move(other)});
    }
    SECTION("Valid")
    {
      Optional<Sentinel> other{Sentinel{}};
      REQUIRE(Opt{other} == WrappedUInt32{Sentinel{}});
      // NOLINTNEXTLINE(performance-move-const-arg) Intentionally testing move behavior
      REQUIRE(Opt{std::move(other)} == WrappedUInt32{Sentinel{}});
    }
  }
  SECTION("In place")
  {
    const Opt opt{std::in_place, 123U};
    REQUIRE(opt == WrappedUInt32{123U});
  }
  SECTION("Different non-optional type")
  {
    const Opt opt{123U};
    REQUIRE(opt == WrappedUInt32{123U});
  }
}

TEST_CASE("Assignment")
{
  Opt opt{};
  REQUIRE(!opt);
  SECTION("From same optional type")
  {
    opt = Opt{123U};
    REQUIRE(opt == WrappedUInt32{123U});
  }
  SECTION("From differen optional type")
  {
    opt = Optional<Sentinel>{Sentinel{}};
    REQUIRE(opt == WrappedUInt32{Sentinel{}});
  }
  SECTION("From same value type")
  {
    opt = WrappedUInt32{123U};
    REQUIRE(opt == WrappedUInt32{123U});
  }
  SECTION("From different value type")
  {
    opt = Sentinel{};
    REQUIRE(opt == WrappedUInt32{Sentinel{}});
  }
  SECTION("Assignable from primitive types")
  {
    tap::Optional<double> maybe_value;
    maybe_value = 5.0;
    REQUIRE(maybe_value);
    REQUIRE(*maybe_value == 5.0);
  }
}

TEST_CASE("Access")
{
  SECTION("operator->()")
  {
    Opt opt{WrappedUInt32{123U}};
    REQUIRE(static_cast<void*>(opt.operator->()) == static_cast<void*>(&opt));
    REQUIRE(static_cast<const void*>(std::as_const(opt).operator->()) == static_cast<const void*>(&std::as_const(opt)));
  }

  SECTION("operator*()")
  {
    Opt opt{WrappedUInt32{123U}};
    REQUIRE(*opt == WrappedUInt32{123U});
    REQUIRE(*std::as_const(opt) == WrappedUInt32{123U});
    REQUIRE(*std::move(opt) == WrappedUInt32{123U});
    REQUIRE(*std::move(std::as_const(opt)) == WrappedUInt32{123U});
    STATIC_REQUIRE(std::is_same_v<decltype(*std::declval<Optional<uint32_t>&>()), uint32_t&>);
    STATIC_REQUIRE(std::is_same_v<decltype(*std::declval<const Optional<uint32_t>&>()), const uint32_t&>);
    STATIC_REQUIRE(std::is_same_v<decltype(*std::declval<Optional<uint32_t>>()), uint32_t&&>);
    STATIC_REQUIRE(std::is_same_v<decltype(*std::declval<const Optional<uint32_t>>()), const uint32_t&&>);
  }

  SECTION("has_value")
  {
    Opt opt{WrappedUInt32{123U}};
    REQUIRE(opt.has_value());
    opt.reset();
    REQUIRE(!opt.has_value());
  }

  SECTION("value")
  {
    Opt opt{WrappedUInt32{123U}};
    REQUIRE(opt.value() == WrappedUInt32{123U});
    REQUIRE(std::as_const(opt).value() == WrappedUInt32{123U});
    REQUIRE(std::move(opt).value() == WrappedUInt32{123U});
    REQUIRE(std::move(std::as_const(opt)).value() == WrappedUInt32{123U});

    opt.value() = WrappedUInt32{321U};
    REQUIRE(opt.value() == WrappedUInt32{321U});
    REQUIRE(std::as_const(opt).value() == WrappedUInt32{321U});
    REQUIRE(std::move(opt).value() == WrappedUInt32{321U});
    REQUIRE(std::move(std::as_const(opt)).value() == WrappedUInt32{321U});

    opt.reset();
    REQUIRE_THROWS(opt.value());
    REQUIRE_THROWS(std::as_const(opt).value());
    REQUIRE_THROWS(std::move(opt).value());
    REQUIRE_THROWS(std::move(std::as_const(opt)).value());

    STATIC_REQUIRE(std::is_same_v<decltype(std::declval<Optional<uint32_t>&>().value()), uint32_t&>);
    STATIC_REQUIRE(std::is_same_v<decltype(std::declval<const Optional<uint32_t>&>().value()), const uint32_t&>);
    STATIC_REQUIRE(std::is_same_v<decltype(std::declval<Optional<uint32_t>>().value()), uint32_t&&>);
    STATIC_REQUIRE(std::is_same_v<decltype(std::declval<const Optional<uint32_t>>().value()), const uint32_t&&>);
  }

  SECTION("value_or")
  {
    Opt opt{WrappedUInt32{123U}};
    const WrappedUInt32 other{321U};
    REQUIRE(opt.value_or(other) == WrappedUInt32{123U});
    opt.reset();
    REQUIRE(opt.value_or(other) == other);
  }
}

TEST_CASE("Swap")
{
  Opt opt{};
  SECTION("nullopt, nullopt")
  {
    Opt other{};
    REQUIRE(!opt);
    REQUIRE(!other);
    opt.swap(other);
    REQUIRE(!opt);
    REQUIRE(!other);
  }
  SECTION("value, nullopt")
  {
    const WrappedUInt32 value{123U};
    opt.emplace(value);
    Opt other{};
    REQUIRE(opt == value);
    REQUIRE(!other);
    opt.swap(other);
    REQUIRE(!opt);
    REQUIRE(other == value);
  }
  SECTION("nullopt, value")
  {
    const WrappedUInt32 value{123U};
    Opt other{value};
    REQUIRE(!opt);
    REQUIRE(other == value);
    opt.swap(other);
    REQUIRE(opt == value);
    REQUIRE(!other);
  }
  SECTION("value, value")
  {
    const WrappedUInt32 value_a{123U};
    const WrappedUInt32 value_b{Sentinel{}};
    opt.emplace(value_a);
    Opt other{value_b};
    REQUIRE(opt == value_a);
    REQUIRE(other == value_b);
    opt.swap(other);
    REQUIRE(opt == value_b);
    REQUIRE(other == value_a);
  }
}

TEST_CASE("Reset")
{
  Opt opt{};
  REQUIRE(std::ranges::all_of(as_bytes(jewels::as_single_item_span(opt)), check_zero));

  opt.emplace(WrappedUInt32{0U});
  REQUIRE(!std::ranges::all_of(as_bytes(jewels::as_single_item_span(opt)), check_zero));
  opt.reset();
  REQUIRE(std::ranges::all_of(as_bytes(jewels::as_single_item_span(opt)), check_zero));

  opt.emplace(Sentinel{});
  REQUIRE(!std::ranges::all_of(as_bytes(jewels::as_single_item_span(opt)), check_zero));
  opt.reset();
  REQUIRE(std::ranges::all_of(as_bytes(jewels::as_single_item_span(opt)), check_zero));
}

TEST_CASE("Emplace")
{
  Opt opt{};
  SECTION("Same type")
  {
    opt.emplace(WrappedUInt32{123U});
    REQUIRE(opt == WrappedUInt32{123U});
  }
  SECTION("Different type")
  {
    opt.emplace(Sentinel{});
    REQUIRE(opt == WrappedUInt32{Sentinel{}});
  }
  SECTION("Return value")
  {
    const WrappedUInt32 value{123U};
    auto& ref = opt.emplace(value);
    REQUIRE(ref == value);
    REQUIRE(opt == value);

    const WrappedUInt32 other_value{321U};
    ref = other_value;
    REQUIRE(ref == other_value);
    REQUIRE(opt == other_value);
  }
}

TEST_CASE("Comparison")
{
  Opt opt{};
  SECTION("Same optional type")
  {
    Opt other{};
    SECTION("nullopt, nullopt")
    {
      REQUIRE(opt == other);
      REQUIRE(!(opt != other));
    }
    SECTION("nullopt, value")
    {
      other.emplace(WrappedUInt32{123U});
      REQUIRE(opt != other);
      REQUIRE(!(opt == other));
    }
    SECTION("value, nullopt")
    {
      opt.emplace(WrappedUInt32{123U});
      REQUIRE(opt != other);
      REQUIRE(!(opt == other));
    }
    SECTION("value == value")
    {
      opt.emplace(WrappedUInt32{123U});
      other.emplace(WrappedUInt32{123U});
      REQUIRE(opt == other);
      REQUIRE(!(opt != other));
    }
    SECTION("value != value")
    {
      opt.emplace(WrappedUInt32{123U});
      other.emplace(WrappedUInt32{321U});
      REQUIRE(opt != other);
      REQUIRE(!(opt == other));
    }
  }
  SECTION("Same value type")
  {
    const WrappedUInt32 value{123U};
    REQUIRE(!(opt == value));
    REQUIRE(!(value == opt));
    REQUIRE(opt != value);
    REQUIRE(value != opt);

    const WrappedUInt32 other_value{321U};
    REQUIRE(opt != other_value);
    REQUIRE(other_value != opt);
    REQUIRE(!(opt == other_value));
    REQUIRE(!(other_value == opt));
  }
  SECTION("std::nullopt")
  {
    REQUIRE(opt == std::nullopt);
    REQUIRE(std::nullopt == opt);
    REQUIRE(!(opt != std::nullopt));
    REQUIRE(!(std::nullopt != opt));
    opt.emplace(WrappedUInt32{123U});
    REQUIRE(opt != std::nullopt);
    REQUIRE(std::nullopt != opt);
    REQUIRE(!(opt == std::nullopt));
    REQUIRE(!(std::nullopt == opt));
  }
}

// Helper classes for make_optional.
class NoArgClass
{
public:
  NoArgClass() = default;
};

class OneArgClass
{
public:
  explicit OneArgClass(NoArgClass /* arg */) {};
};

class TwoArgClass
{
public:
  TwoArgClass(NoArgClass /* arg1 */, int32_t /* arg2 */) {};
};

TEST_CASE("make_optional")
{
  SECTION("rvalue int")
  {
    const Optional<int32_t> opt = make_optional(32);
    REQUIRE(opt.has_value());
    REQUIRE(opt.value() == 32);
  }

  SECTION("lvalue int")
  {
    const int32_t lvalue_int = 32;
    const Optional<int32_t> lvalue_opt = make_optional(lvalue_int);
    REQUIRE(lvalue_opt.has_value());
    REQUIRE(lvalue_opt.value() == 32);
  }

  SECTION("rvalue class")
  {
    const Optional<NoArgClass> opt = make_optional(NoArgClass());
    REQUIRE(opt.has_value());
  }

  SECTION("lvalue class")
  {
    const NoArgClass obj;
    const Optional<NoArgClass> opt = make_optional(obj);
    REQUIRE(opt.has_value());
  }

  SECTION("move-only type")
  {
    auto opt = make_optional(std::make_unique<int32_t>(42));
    REQUIRE(opt.has_value());
    REQUIRE(*opt.value() == 42);
  }

  SECTION("NoArgClass via in_place")
  {
    auto opt = make_optional<NoArgClass>(std::in_place);
    REQUIRE(opt.has_value());
  }

  SECTION("OneArgClass via in_place")
  {
    auto opt = make_optional<OneArgClass>(std::in_place, NoArgClass{});
    REQUIRE(opt.has_value());
  }

  SECTION("TwoArgClass via in_place")
  {
    auto opt = make_optional<TwoArgClass>(std::in_place, NoArgClass{}, 42);
    REQUIRE(opt.has_value());
  }
}

} // namespace jewels::tap
