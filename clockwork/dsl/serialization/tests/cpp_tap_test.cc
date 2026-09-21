// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/dsl/tests/support/tapmsg.hh"
#include "clockwork/dsl/tests/support/taptag.hh"
#include "clockwork/dsl/tests/support/taptags_clk_cc.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/container/at.hh"
#include "jewels/container/compare.hh"
#include "jewels/container/tap/optional.hh"
#include "jewels/container/tap/var_array.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/std/expected.hh"
#include "jewels/std/span.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/utility/enum_flags.hh"
#include "jewels/utility/fix_clockwork_path.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

// Manual import / aliases to avoid symbols being in the global namespace and not being caught in the test.
template <class T>
using Tap = ::clockwork::Tap<T>;
template <class T>
using Tachyon = ::clockwork::Tachyon<T>;
template <class T>
using Tappy = ::clockwork::Tappy<T>;
template <class T>
using TapInit = clockwork::TapInit<T>;
using SampleTag = ::clockwork::testing::tags::SampleTag;
using SomeEnum = ::clockwork::testing::SomeEnum;
using SomeFlags = ::clockwork::testing::SomeFlags;
using AnotherTag = ::clockwork::testing::separate_tags::AnotherTag;
using PaddedMsg = ::clockwork::testing::PaddedMsg;
using SubMsg = ::clockwork::testing::SubMsg;
constexpr int32_t signed_value = 234;
using TapMsg = ::clockwork::testing::TapMsg<signed_value>;
using TapMsg2 = ::clockwork::testing::TapMsg2;
template <auto value, class T>
using GenericSubMsg = ::clockwork::testing::GenericSubMsg<value, T>;
template <auto value, class T>
using GenericMsg = ::clockwork::testing::GenericMsg<value, T>;
using TapMsgAlias = ::clockwork::testing::TapMsgAlias;
using OutOfOrderFields = ::clockwork::testing::OutOfOrderFields;
using NoConstructor = ::clockwork::testing::NoConstructor;
template <class T>
using ParamAsField = ::clockwork::testing::ParamAsField<T>;
template <auto param>
using GenericValuesOnly = ::clockwork::testing::GenericValuesOnly<param>;

size_t file_size(const std::string& path)
{
  std::ifstream file{path, std::ios::binary | std::ios::ate};
  auto size = file.tellg();
  REQUIRE(size >= 0);
  return static_cast<size_t>(size);
}

std::vector<char> read_binary_file(const std::string& path)
{
  const auto fixed_path = jewels::fix_clockwork_path(path);
  std::ifstream file{fixed_path, std::ios::binary};
  std::vector<char> bytes{};
  bytes.resize(file_size(fixed_path));
  file.read(bytes.data(), static_cast<int64_t>(bytes.size()));
  return bytes;
}

TEST_CASE("Tachyon")
{
  SECTION("No generics")
  {
    using Type = Tachyon<TapMsg>;
    STATIC_REQUIRE(offsetof(Type, array_of_primitives) == 0UL);
    STATIC_REQUIRE(sizeof(std::declval<Type>().array_of_primitives) == 48UL);
    STATIC_REQUIRE(
      std::is_same_v<decltype(std::declval<Type>().array_of_primitives), ::jewels::tap::VarArray<int32_t, 9U>>);

    STATIC_REQUIRE(offsetof(Type, array_of_array) == 48UL);
    STATIC_REQUIRE(sizeof(std::declval<Type>().array_of_array) == 40UL);
    STATIC_REQUIRE(
      std::is_same_v<
        decltype(std::declval<Type>().array_of_array),
        ::jewels::tap::VarArray<::jewels::tap::VarString<3U>, 2U>>);

    STATIC_REQUIRE(offsetof(Type, array_of_schema) == 88UL);
    STATIC_REQUIRE(sizeof(std::declval<Type>().array_of_schema) == 24UL);
    STATIC_REQUIRE(
      std::is_same_v<decltype(std::declval<Type>().array_of_schema), ::jewels::tap::VarArray<Tappy<SubMsg>, 2U>>);

    STATIC_REQUIRE(offsetof(Type, uuid) == 112UL);
    STATIC_REQUIRE(sizeof(std::declval<Type>().uuid) == 16UL);
    STATIC_REQUIRE(std::is_same_v<decltype(std::declval<Type>().uuid), ::jewels::Uuid<SubMsg>>);

    STATIC_REQUIRE(offsetof(Type, uuid_different_namespace) == 128UL);
    STATIC_REQUIRE(sizeof(std::declval<Type>().uuid_different_namespace) == 16UL);
    STATIC_REQUIRE(std::is_same_v<decltype(std::declval<Type>().uuid_different_namespace), ::jewels::Uuid<AnotherTag>>);

    STATIC_REQUIRE(offsetof(Type, var_string) == 144UL);
    STATIC_REQUIRE(sizeof(std::declval<Type>().var_string) == 16UL);
    STATIC_REQUIRE(std::is_same_v<decltype(std::declval<Type>().var_string), ::jewels::tap::VarString<2UL>>);

    STATIC_REQUIRE(offsetof(Type, integer) == 160UL);
    STATIC_REQUIRE(sizeof(std::declval<Type>().integer) == 8UL);
    STATIC_REQUIRE(std::is_same_v<decltype(std::declval<Type>().integer), int64_t>);

    STATIC_REQUIRE(offsetof(Type, nested_schema) == 168UL);
    STATIC_REQUIRE(sizeof(std::declval<Type>().nested_schema) == 8UL);
    STATIC_REQUIRE(std::is_same_v<decltype(std::declval<Type>().nested_schema), Tappy<SubMsg>>);

    STATIC_REQUIRE(offsetof(Type, duration) == 176UL);
    STATIC_REQUIRE(sizeof(std::declval<Type>().duration) == 8UL);
    STATIC_REQUIRE(std::is_same_v<decltype(std::declval<Type>().duration), std::chrono::nanoseconds>);

    STATIC_REQUIRE(offsetof(Type, sync_time) == 184UL);
    STATIC_REQUIRE(sizeof(std::declval<Type>().sync_time) == 8UL);
    STATIC_REQUIRE(std::is_same_v<decltype(std::declval<Type>().sync_time), ::jewels::time::SyncTime>);

    STATIC_REQUIRE(offsetof(Type, strong_type) == 192UL);
    STATIC_REQUIRE(sizeof(std::declval<Type>().strong_type) == 8UL);
    STATIC_REQUIRE(std::is_same_v<decltype(std::declval<Type>().strong_type), uint64_t>);

    STATIC_REQUIRE(offsetof(Type, optional) == 200UL);
    STATIC_REQUIRE(sizeof(std::declval<Type>().optional) == 8UL);
    STATIC_REQUIRE(std::is_same_v<decltype(std::declval<Type>().optional), ::jewels::tap::Optional<uint32_t>>);

    STATIC_REQUIRE(offsetof(Type, fixed_array) == 208UL);
    STATIC_REQUIRE(sizeof(std::declval<Type>().fixed_array) == 8UL);
    STATIC_REQUIRE(std::is_same_v<decltype(std::declval<Type>().fixed_array), ::std::array<int32_t, 2U>>);

    STATIC_REQUIRE(offsetof(Type, floating_point) == 216UL);
    STATIC_REQUIRE(sizeof(std::declval<Type>().floating_point) == 4UL);
    STATIC_REQUIRE(std::is_same_v<decltype(std::declval<Type>().floating_point), float>);

    STATIC_REQUIRE(offsetof(Type, external_strong_type) == 220UL);
    STATIC_REQUIRE(sizeof(std::declval<Type>().external_strong_type) == 4UL);
    STATIC_REQUIRE(
      std::is_same_v<decltype(std::declval<Type>().external_strong_type), ::clockwork::external::ExternalStrongType>);

    STATIC_REQUIRE(offsetof(Type, boolean) == 228UL);
    STATIC_REQUIRE(sizeof(std::declval<Type>().boolean) == 1UL);
    STATIC_REQUIRE(std::is_same_v<decltype(std::declval<Type>().boolean), bool>);

    STATIC_REQUIRE(offsetof(Type, default_enum) == 229UL);
    STATIC_REQUIRE(sizeof(std::declval<Type>().default_enum) == 1UL);
    STATIC_REQUIRE(std::is_same_v<decltype(std::declval<Type>().default_enum), SomeEnum>);

    STATIC_REQUIRE(offsetof(Type, enum_with_init) == 230UL);
    STATIC_REQUIRE(sizeof(std::declval<Type>().enum_with_init) == 1UL);
    STATIC_REQUIRE(std::is_same_v<decltype(std::declval<Type>().enum_with_init), SomeEnum>);

    STATIC_REQUIRE(offsetof(Type, bool_with_init) == 231UL);
    STATIC_REQUIRE(sizeof(std::declval<Type>().bool_with_init) == 1UL);
    STATIC_REQUIRE(std::is_same_v<decltype(std::declval<Type>().bool_with_init), bool>);

    STATIC_REQUIRE(offsetof(Type, default_flags) == 232UL);
    STATIC_REQUIRE(sizeof(std::declval<Type>().default_flags) == 1UL);
    STATIC_REQUIRE(std::is_same_v<decltype(std::declval<Type>().default_flags), SomeFlags>);

    STATIC_REQUIRE(offsetof(Type, flags_with_init) == 233UL);
    STATIC_REQUIRE(sizeof(std::declval<Type>().flags_with_init) == 1UL);
    STATIC_REQUIRE(std::is_same_v<decltype(std::declval<Type>().flags_with_init), SomeFlags>);

    STATIC_REQUIRE(offsetof(Type, integer_with_init) == 224UL);
    STATIC_REQUIRE(sizeof(std::declval<Type>().integer_with_init) == 4UL);
    STATIC_REQUIRE(std::is_same_v<decltype(std::declval<Type>().integer_with_init), int32_t>);

    STATIC_REQUIRE(offsetof(Type, padding_0_) == 234UL);
    STATIC_REQUIRE(sizeof(std::declval<Type>().padding_0_) == 6UL);
    STATIC_REQUIRE(std::is_same_v<decltype(std::declval<Type>().padding_0_), std::array<std::byte, 6UL>>);

    STATIC_REQUIRE(alignof(Type) == 8UL);
    STATIC_REQUIRE(
      sizeof(Type) == sizeof(std::declval<Type>().integer) + sizeof(std::declval<Type>().floating_point) +
                        sizeof(std::declval<Type>().array_of_primitives) + sizeof(std::declval<Type>().array_of_array) +
                        sizeof(std::declval<Type>().boolean) + sizeof(std::declval<Type>().uuid) +
                        sizeof(std::declval<Type>().uuid_different_namespace) +
                        sizeof(std::declval<Type>().var_string) + sizeof(std::declval<Type>().fixed_array) +
                        sizeof(std::declval<Type>().default_enum) + sizeof(std::declval<Type>().enum_with_init) +
                        sizeof(std::declval<Type>().default_flags) + sizeof(std::declval<Type>().flags_with_init) +
                        sizeof(std::declval<Type>().nested_schema) + sizeof(std::declval<Type>().duration) +
                        sizeof(std::declval<Type>().sync_time) + sizeof(std::declval<Type>().strong_type) +
                        sizeof(std::declval<Type>().external_strong_type) + sizeof(std::declval<Type>().optional) +
                        sizeof(std::declval<Type>().array_of_schema) + sizeof(std::declval<Type>().bool_with_init) +
                        sizeof(std::declval<Type>().integer_with_init) + sizeof(std::declval<Type>().padding_0_));
  }
  SECTION("Generic")
  {
    using Type = Tachyon<GenericMsg<3UL, bool>>;
    STATIC_REQUIRE(offsetof(Type, array_of_parameterized_type) == 0UL);
    STATIC_REQUIRE(sizeof(std::declval<Type>().array_of_parameterized_type) == 16UL);
    STATIC_REQUIRE(
      std::is_same_v<decltype(std::declval<Type>().array_of_parameterized_type), ::jewels::tap::VarArray<bool, 3UL>>);

    STATIC_REQUIRE(offsetof(Type, field) == 16UL);
    STATIC_REQUIRE(sizeof(std::declval<Type>().field) == 8UL);
    STATIC_REQUIRE(std::is_same_v<decltype(std::declval<Type>().field), uint64_t>);

    STATIC_REQUIRE(alignof(Type) == 8UL);

    STATIC_REQUIRE(
      sizeof(Type) == sizeof(std::declval<Type>().array_of_parameterized_type) + sizeof(std::declval<Type>().field));
  }
  SECTION("Generic with sub-schema")
  {
    using Type = Tachyon<GenericMsg<3UL, SubMsg>>;
    STATIC_REQUIRE(offsetof(Type, array_of_parameterized_type) == 0UL);
    STATIC_REQUIRE(sizeof(std::declval<Type>().array_of_parameterized_type) == 32UL);
    STATIC_REQUIRE(
      std::is_same_v<
        decltype(std::declval<Type>().array_of_parameterized_type),
        ::jewels::tap::VarArray<Tappy<SubMsg>, 3UL>>);

    STATIC_REQUIRE(offsetof(Type, field) == 32UL);
    STATIC_REQUIRE(sizeof(std::declval<Type>().field) == 8UL);
    STATIC_REQUIRE(std::is_same_v<decltype(std::declval<Type>().field), uint64_t>);

    STATIC_REQUIRE(alignof(Type) == 8UL);

    STATIC_REQUIRE(
      sizeof(Type) == sizeof(std::declval<Type>().array_of_parameterized_type) + sizeof(std::declval<Type>().field));
  }
  SECTION("Generic with generic sub-schema")
  {
    using Type = Tachyon<GenericMsg<3UL, GenericSubMsg<3UL, bool>>>;
    STATIC_REQUIRE(std::is_same_v<Tap<Type>, ::clockwork::testing::GenericTapMsgAlias>);
    STATIC_REQUIRE(offsetof(Type, array_of_parameterized_type) == 0UL);
    STATIC_REQUIRE(sizeof(std::declval<Type>().array_of_parameterized_type) == 56UL);
    STATIC_REQUIRE(
      std::is_same_v<
        decltype(std::declval<Type>().array_of_parameterized_type),
        ::jewels::tap::VarArray<Tappy<GenericSubMsg<3UL, bool>>, 3UL>>);

    STATIC_REQUIRE(offsetof(Type, field) == 56UL);
    STATIC_REQUIRE(sizeof(std::declval<Type>().field) == 8UL);
    STATIC_REQUIRE(std::is_same_v<decltype(std::declval<Type>().field), uint64_t>);

    STATIC_REQUIRE(alignof(Type) == 8UL);

    STATIC_REQUIRE(
      sizeof(Type) == sizeof(std::declval<Type>().array_of_parameterized_type) + sizeof(std::declval<Type>().field));
  }
  SECTION("Generic with param direclty as a field")
  {
    SECTION("Primitive param")
    {
      using Type = Tachyon<ParamAsField<uint64_t>>;
      STATIC_REQUIRE(offsetof(Type, value) == 0UL);
      STATIC_REQUIRE(std::is_same_v<decltype(std::declval<Type>().value), uint64_t>);
    }
    SECTION("Schema param")
    {
      using Type = Tachyon<ParamAsField<SubMsg>>;
      STATIC_REQUIRE(offsetof(Type, value) == 0UL);
      STATIC_REQUIRE(std::is_same_v<decltype(std::declval<Type>().value), Tappy<SubMsg>>);
    }
  }
  SECTION("Generic from constant")
  {
    using Type = Tachyon<::clockwork::testing::FromConstant<4UL>>;
    STATIC_REQUIRE(offsetof(Type, as_type) == 0UL);
    STATIC_REQUIRE(std::is_same_v<decltype(std::declval<Type>().as_type), ::jewels::tap::VarArray<uint64_t, 4UL>>);
    STATIC_REQUIRE(offsetof(Type, as_init) == sizeof(::jewels::tap::VarArray<uint64_t, 4UL>));
    STATIC_REQUIRE(std::is_same_v<decltype(std::declval<Type>().as_init), uint64_t>);
    STATIC_REQUIRE(std::is_same_v<Tap<Type>, ::clockwork::testing::FromConstantFour>);
  }
}

TEST_CASE("Interface aliases")
{
  STATIC_REQUIRE(std::is_same_v<TapMsgAlias, Tappy<TapMsg>>);
}

TEST_CASE("Tags")
{
  static_cast<void>(TapMsg{});
  static_cast<void>(SampleTag{});
}

TEST_CASE("Methods")
{
  Tappy<TapMsg> msg{};
  SECTION("Primitives")
  {
    REQUIRE(msg.get_integer() == 0UL);
    msg.set_integer(123L);
    REQUIRE(msg.get_integer() == 123L);
    REQUIRE(msg.get_mutable_integer() == 123L);
    SECTION("assign via get")
    {
      msg.get_mutable_integer() = 456L;
      REQUIRE(msg.get_integer() == 456L);
    }
    SECTION("assign via get_mutable")
    {
      msg.get_mutable_integer() = 456L;
      REQUIRE(msg.get_integer() == 456L);
    }
  }
  SECTION("VarArray")
  {
    STATIC_REQUIRE(
      std::is_same_v<decltype(std::declval<Tappy<TapMsg>&>().get_array_of_primitives()), std::span<const int32_t>>);
    REQUIRE(msg.get_array_of_primitives().empty());
    SECTION("Modify using get_underlying")
    {
      STATIC_REQUIRE(
        std::is_same_v<
          decltype(std::declval<Tappy<TapMsg>&>().get_underlying_array_of_primitives()),
          ::jewels::tap::VarArray<int32_t, 9>&>);
      ::jewels::tap::VarArray<int32_t, 9> var_array{};
      var_array.emplace_back(123);
      msg.get_underlying_array_of_primitives() = var_array;
      REQUIRE(msg.get_array_of_primitives().size() == 1UL);
      REQUIRE(jewels::at(msg.get_array_of_primitives(), 0L) == 123);
    }
    SECTION("Modify using get_mutable")
    {
      STATIC_REQUIRE(
        std::is_same_v<decltype(std::declval<Tappy<TapMsg>&>().get_mutable_array_of_primitives()), std::span<int32_t>>);
      msg.get_underlying_array_of_primitives().resize(2UL);
      ::std::ranges::copy(::std::ranges::views::iota(1UL, 3UL), std::begin(msg.get_mutable_array_of_primitives()));
      REQUIRE(!::std::ranges::all_of(msg.get_array_of_primitives(), [](auto value) { return value == 0; }));
      REQUIRE(::std::ranges::equal(::std::ranges::views::iota(1UL, 3UL), msg.get_array_of_primitives()));
    }
  }
  SECTION("VarString")
  {
    STATIC_REQUIRE(std::is_same_v<decltype(std::declval<Tappy<TapMsg>&>().get_var_string()), std::string_view>);
    REQUIRE(msg.get_var_string() == std::string_view{""});
    SECTION("Modify using get_underlying")
    {
      STATIC_REQUIRE(
        std::is_same_v<
          decltype(std::declval<Tappy<TapMsg>&>().get_underlying_var_string()),
          ::jewels::tap::VarString<2UL>&>);
      ::jewels::tap::VarString<2UL> var_string{};
      REQUIRE(var_string.try_set("a"));
      msg.get_underlying_var_string() = var_string;
      REQUIRE(msg.get_var_string().size() == 1UL);
      REQUIRE(msg.get_var_string() == "a");
      const auto cmsg = msg;
      REQUIRE(var_string.try_set("q"));
      msg.get_underlying_var_string() = var_string;
      CHECK(msg.get_var_string() == "q");
      msg.get_underlying_var_string() = cmsg.get_underlying_var_string();
      CHECK(msg.get_var_string() == "a");
    }
    SECTION("Modify using get_mutable")
    {
      STATIC_REQUIRE(
        std::is_same_v<decltype(std::declval<Tappy<TapMsg>&>().get_mutable_var_string()), std::span<char>>);
      msg.get_underlying_array_of_primitives().resize(2UL);
      ::std::ranges::copy(::std::ranges::views::iota(1UL, 3UL), std::begin(msg.get_mutable_array_of_primitives()));
      REQUIRE(!::std::ranges::all_of(msg.get_array_of_primitives(), [](auto value) { return value == 0; }));
      REQUIRE(::std::ranges::equal(::std::ranges::views::iota(1UL, 3UL), msg.get_array_of_primitives()));
    }
  }
  SECTION("FixedArray")
  {
    STATIC_REQUIRE(
      std::is_same_v<decltype(std::declval<Tappy<TapMsg>&>().get_fixed_array()), std::span<const int32_t, 2UL>>);
    REQUIRE(msg.get_fixed_array().size() == 2UL);
    REQUIRE(msg.get_mutable_fixed_array().size() == 2UL);
    REQUIRE(::std::ranges::all_of(msg.get_fixed_array(), [](auto value) { return value == 0; }));
    SECTION("Modify using get_mutable")
    {
      STATIC_REQUIRE(
        std::is_same_v<decltype(std::declval<Tappy<TapMsg>&>().get_mutable_fixed_array()), std::span<int32_t, 2UL>>);
      ::std::ranges::copy(::std::ranges::views::iota(1UL, 3UL), std::begin(msg.get_mutable_fixed_array()));
      REQUIRE(!::std::ranges::all_of(msg.get_fixed_array(), [](auto value) { return value == 0; }));
      REQUIRE(::std::ranges::equal(::std::ranges::views::iota(1UL, 3UL), msg.get_fixed_array()));
    }
    SECTION("Modify using set")
    {
      std::array<int32_t, 2UL> data{3, 7};
      msg.set_fixed_array(std::span{data});
      REQUIRE(!::std::ranges::all_of(msg.get_fixed_array(), [](auto value) { return value == 0; }));
      REQUIRE(::std::ranges::equal(std::span{data}, msg.get_fixed_array()));
    }
  }
  SECTION("Optional")
  {
    STATIC_REQUIRE(std::is_same_v<decltype(std::declval<Tappy<TapMsg>&>().value_optional()), const uint32_t&>);
    STATIC_REQUIRE(std::is_same_v<decltype(std::declval<Tappy<TapMsg>&>().value_mutable_optional()), uint32_t&>);
    REQUIRE(!msg.has_optional());
    REQUIRE_THROWS(msg.value_optional());
    REQUIRE_THROWS(msg.value_mutable_optional());
    SECTION("Modify using set")
    {
      msg.set_optional(123U);
      REQUIRE(msg.has_optional());
      REQUIRE(msg.value_optional() == 123U);
      REQUIRE(msg.value_mutable_optional() == 123U);
      SECTION("Modify using value_mutable")
      {
        msg.value_mutable_optional() = 456U;
        REQUIRE(msg.has_optional());
        REQUIRE(msg.value_optional() == 456U);
        REQUIRE(msg.value_mutable_optional() == 456U);
      }
      SECTION("Modify using set from optional")
      {
        msg.setopt_optional(std::optional<uint32_t>{567U});
        REQUIRE(msg.has_optional());
        REQUIRE(msg.value_optional() == 567U);
        REQUIRE(msg.value_mutable_optional() == 567U);
      }
      SECTION("Clear value")
      {
        REQUIRE(msg.has_optional());
        msg.reset_optional();
        REQUIRE(!msg.has_optional());
      }
      SECTION("Clear from std::optional")
      {
        REQUIRE(msg.has_optional());
        msg.setopt_optional(std::nullopt);
        REQUIRE(!msg.has_optional());
      }
    }
  }
  SECTION("Schema.clear")
  {
    const Tappy<TapMsg> empty{};
    Tappy<TapMsg> test{};
    // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) this also checks the 'unused' area
    REQUIRE(std::memcmp(&empty, &test, sizeof(empty)) == 0);
    test.clear();
    // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) this also checks the 'unused' area
    CHECK(std::memcmp(&empty, &test, sizeof(empty)) == 0);

    test.set_integer(342);
    test.set_floating_point(264.);
    test.set_boolean(true);
    REQUIRE(test.try_set_array_of_primitives(std::array<int32_t, 4>{5, 6, 7, 8}));
    REQUIRE(test.try_set_array_of_array(std::array<jewels::tap::VarString<3>, 1>{jewels::tap::VarString<3>("aa")}));
    test.set_uuid(*::jewels::Uuid<SubMsg>::from_string("11111111111111111111111111111111"));
    test.set_default_enum(SomeEnum::second_value);
    test.set_enum_with_init(SomeEnum::first_value);
    test.set_default_flags(SomeFlags::flag1);
    test.set_flags_with_init(SomeFlags::flag1);
    test.get_mutable_nested_schema().set_field(342);
    test.get_underlying_array_of_schema().emplace_back();
    test.get_underlying_array_of_schema()[0].set_field(1342);
    test.set_duration(std::chrono::nanoseconds(123456));
    test.set_optional(6);
    test.set_bool_with_init(false);
    test.set_fixed_array(std::array<int32_t, 2>{3, 4});
    REQUIRE(test.try_set_var_string("a"));
    test.set_integer_with_init(894568);
    // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) this also checks the 'unused' area
    CHECK(std::memcmp(&empty, &test, sizeof(empty)) != 0);

    test.clear();
    // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) this also checks the 'unused' area
    CHECK(std::memcmp(&empty, &test, sizeof(empty)) == 0);
  }
}

TEST_CASE("More methods")
{
  Tappy<TapMsg2> msg{};
  SECTION("Optional<FixedArray>")
  {
    STATIC_REQUIRE(
      std::is_same_v<
        decltype(std::declval<Tappy<TapMsg2>&>().value_optional_fixed_array()),
        std::span<const uint32_t, 4U>>);
    STATIC_REQUIRE(
      std::is_same_v<
        decltype(std::declval<Tappy<TapMsg2>&>().value_mutable_optional_fixed_array()),
        std::span<uint32_t, 4U>>);
    REQUIRE(!msg.has_optional_fixed_array());
    REQUIRE_THROWS(msg.value_optional_fixed_array());
    REQUIRE_THROWS(msg.value_mutable_optional_fixed_array());
    SECTION("Modify using set")
    {
      const std::array<uint32_t, 4U> value1{123U, 234U, 345U, 456U};
      msg.set_optional_fixed_array(value1);
      REQUIRE(msg.has_optional_fixed_array());
      REQUIRE(std::ranges::equal(msg.value_optional_fixed_array(), value1));
      REQUIRE(std::ranges::equal(msg.value_mutable_optional_fixed_array(), value1));
      SECTION("Modify using value_mutable")
      {
        const std::array<uint32_t, 4U> value2{234U, 345U, 456U, 567U};
        std::ranges::copy(value2, msg.value_mutable_optional_fixed_array().begin());
        REQUIRE(msg.has_optional_fixed_array());
        REQUIRE(std::ranges::equal(msg.value_optional_fixed_array(), value2));
        REQUIRE(std::ranges::equal(msg.value_mutable_optional_fixed_array(), value2));
      }
      SECTION("Modify using set from std::optional")
      {
        const std::array<uint32_t, 4U> value3{345U, 456U, 567U, 678U};
        msg.setopt_optional_fixed_array(value3);
        REQUIRE(msg.has_optional_fixed_array());
        REQUIRE(std::ranges::equal(msg.value_optional_fixed_array(), value3));
        REQUIRE(std::ranges::equal(msg.value_mutable_optional_fixed_array(), value3));
      }
      SECTION("Clear value")
      {
        REQUIRE(msg.has_optional_fixed_array());
        msg.reset_optional_fixed_array();
        REQUIRE(!msg.has_optional_fixed_array());
      }
      SECTION("Clear from std::optional")
      {
        REQUIRE(msg.has_optional_fixed_array());
        msg.setopt_optional_fixed_array(std::nullopt);
        REQUIRE(!msg.has_optional_fixed_array());
      }
    }
  }
  SECTION("Optional<VarArray>")
  {
    STATIC_REQUIRE(
      std::is_same_v<decltype(std::declval<Tappy<TapMsg2>&>().value_optional_var_array()), std::span<const uint32_t>>);
    STATIC_REQUIRE(
      std::
        is_same_v<decltype(std::declval<Tappy<TapMsg2>&>().value_mutable_optional_var_array()), std::span<uint32_t>>);
    REQUIRE(!msg.has_optional_var_array());
    REQUIRE_THROWS(msg.value_optional_var_array());
    REQUIRE_THROWS(msg.value_mutable_optional_var_array());
    SECTION("Modify using try_set")
    {
      const std::array<uint32_t, 4U> value1{123U, 234U, 345U, 456U};
      REQUIRE(msg.try_set_optional_var_array(value1));
      REQUIRE(msg.has_optional_var_array());
      REQUIRE(std::ranges::equal(msg.value_optional_var_array(), value1));
      REQUIRE(std::ranges::equal(msg.value_mutable_optional_var_array(), value1));
      SECTION("Modify using value_mutable")
      {
        const std::array<uint32_t, 4U> value2{234U, 345U, 456U, 567U};
        std::ranges::copy(value2, msg.value_mutable_optional_var_array().begin());
        REQUIRE(msg.has_optional_var_array());
        REQUIRE(std::ranges::equal(msg.value_optional_var_array(), value2));
        REQUIRE(std::ranges::equal(msg.value_mutable_optional_var_array(), value2));
      }
      SECTION("Modify using try_set from std::optional")
      {
        const std::array<uint32_t, 4U> value3{345U, 456U, 567U, 678U};
        REQUIRE(msg.try_setopt_optional_var_array(value3));
        REQUIRE(msg.has_optional_var_array());
        REQUIRE(std::ranges::equal(msg.value_optional_var_array(), value3));
        REQUIRE(std::ranges::equal(msg.value_mutable_optional_var_array(), value3));
      }
      SECTION("Clear value")
      {
        REQUIRE(msg.has_optional_var_array());
        msg.reset_optional_var_array();
        REQUIRE(!msg.has_optional_var_array());
      }
      SECTION("Clear from std::optional")
      {
        REQUIRE(msg.has_optional_var_array());
        REQUIRE(msg.try_setopt_optional_var_array(std::nullopt));
        REQUIRE(!msg.has_optional_var_array());
      }
    }
  }
  SECTION("Optional<VarString>")
  {
    STATIC_REQUIRE(
      std::is_same_v<decltype(std::declval<Tappy<TapMsg2>&>().value_optional_var_string()), std::string_view>);
    STATIC_REQUIRE(
      std::is_same_v<decltype(std::declval<Tappy<TapMsg2>&>().value_mutable_optional_var_string()), std::span<char>>);
    REQUIRE(!msg.has_optional_var_string());
    REQUIRE_THROWS(msg.value_optional_var_string());
    REQUIRE_THROWS(msg.value_mutable_optional_var_string());
    SECTION("Modify using try_set")
    {
      const std::string_view value1 = "123";
      REQUIRE(msg.try_set_optional_var_string(value1));
      REQUIRE(msg.has_optional_var_string());
      REQUIRE(std::ranges::equal(msg.value_optional_var_string(), value1));
      REQUIRE(std::ranges::equal(msg.value_mutable_optional_var_string(), value1));
      SECTION("Modify using value_mutable")
      {
        const std::string_view value2 = "ABC";
        std::ranges::copy(value2, msg.value_mutable_optional_var_string().begin());
        REQUIRE(msg.has_optional_var_string());
        REQUIRE(std::ranges::equal(msg.value_optional_var_string(), value2));
        REQUIRE(std::ranges::equal(msg.value_mutable_optional_var_string(), value2));
      }
      SECTION("Modify using try_set from std::optional")
      {
        const std::string_view value3 = "abc";
        REQUIRE(msg.try_setopt_optional_var_string(value3));
        REQUIRE(msg.has_optional_var_string());
        REQUIRE(std::ranges::equal(msg.value_optional_var_string(), value3));
        REQUIRE(std::ranges::equal(msg.value_mutable_optional_var_string(), value3));
      }
      SECTION("Clear value")
      {
        REQUIRE(msg.has_optional_var_string());
        msg.reset_optional_var_string();
        REQUIRE(!msg.has_optional_var_string());
      }
      SECTION("Clear from std::optional")
      {
        REQUIRE(msg.has_optional_var_string());
        REQUIRE(msg.try_setopt_optional_var_string(std::nullopt));
        REQUIRE(!msg.has_optional_var_string());
      }
    }
  }
  SECTION("Optional<Uuid>")
  {
    STATIC_REQUIRE(
      std::is_same_v<decltype(std::declval<Tappy<TapMsg2>&>().value_optional_uuid()), const ::jewels::Uuid<SubMsg>&>);
    STATIC_REQUIRE(
      std::is_same_v<decltype(std::declval<Tappy<TapMsg2>&>().value_mutable_optional_uuid()), ::jewels::Uuid<SubMsg>&>);
    REQUIRE(!msg.has_optional_uuid());
    REQUIRE_THROWS(msg.value_optional_uuid());
    REQUIRE_THROWS(msg.value_mutable_optional_uuid());
    SECTION("Modify using set")
    {
      const auto uuid1 = ::jewels::Uuid<SubMsg>::from_string("11111111111111111111111111111111");
      REQUIRE(uuid1);
      msg.set_optional_uuid(*uuid1);
      REQUIRE(msg.has_optional_uuid());
      REQUIRE(msg.value_optional_uuid() == *uuid1);
      REQUIRE(msg.value_mutable_optional_uuid() == *uuid1);
      SECTION("Modify using value_mutable")
      {
        const auto uuid2 = ::jewels::Uuid<SubMsg>::from_string("22222222222222222222222222222222");
        REQUIRE(uuid2);
        msg.value_mutable_optional_uuid() = *uuid2;
        REQUIRE(msg.has_optional_uuid());
        REQUIRE(msg.value_optional_uuid() == *uuid2);
        REQUIRE(msg.value_mutable_optional_uuid() == *uuid2);
      }
      SECTION("Modify using set from optional")
      {
        const auto uuid3 = ::jewels::Uuid<SubMsg>::from_string("33333333333333333333333333333333");
        REQUIRE(uuid3);
        msg.setopt_optional_uuid(std::optional<::jewels::Uuid<SubMsg>>{*uuid3});
        REQUIRE(msg.has_optional_uuid());
        REQUIRE(msg.value_optional_uuid() == *uuid3);
        REQUIRE(msg.value_mutable_optional_uuid() == *uuid3);
      }
      SECTION("Clear value")
      {
        REQUIRE(msg.has_optional_uuid());
        msg.reset_optional_uuid();
        REQUIRE(!msg.has_optional_uuid());
      }
      SECTION("Clear from std::optional")
      {
        REQUIRE(msg.has_optional_uuid());
        msg.setopt_optional_uuid(std::nullopt);
        REQUIRE(!msg.has_optional_uuid());
      }
    }
  }
  SECTION("Optional<Bool>")
  {
    STATIC_REQUIRE(std::is_same_v<decltype(std::declval<Tappy<TapMsg2>&>().value_optional_bool()), const bool&>);
    STATIC_REQUIRE(std::is_same_v<decltype(std::declval<Tappy<TapMsg2>&>().value_mutable_optional_bool()), bool&>);
    REQUIRE(!msg.has_optional_bool());
    REQUIRE_THROWS(msg.value_optional_bool());
    REQUIRE_THROWS(msg.value_mutable_optional_bool());
    SECTION("Modify using set")
    {
      msg.set_optional_bool(true);
      REQUIRE(msg.has_optional_bool());
      REQUIRE(msg.value_optional_bool());
      REQUIRE(msg.value_mutable_optional_bool());
      SECTION("Modify using value_mutable")
      {
        msg.value_mutable_optional_bool() = false;
        REQUIRE(msg.has_optional_bool());
        REQUIRE(!msg.value_optional_bool());
        REQUIRE(!msg.value_mutable_optional_bool());
      }
      SECTION("Modify using set from optional")
      {
        msg.setopt_optional_bool(true);
        REQUIRE(msg.has_optional_bool());
        REQUIRE(msg.value_optional_bool());
        REQUIRE(msg.value_mutable_optional_bool());
      }
      SECTION("Clear value")
      {
        REQUIRE(msg.has_optional_bool());
        msg.reset_optional_bool();
        REQUIRE(!msg.has_optional_bool());
      }
      SECTION("Clear from std::optional")
      {
        REQUIRE(msg.has_optional_bool());
        msg.setopt_optional_bool(std::nullopt);
        REQUIRE(!msg.has_optional_bool());
      }
    }
  }
  SECTION("Optional<MyStrongType>")
  {
    STATIC_REQUIRE(
      std::is_same_v<decltype(std::declval<Tappy<TapMsg2>&>().value_optional_strong_type()), const uint64_t&>);
    STATIC_REQUIRE(
      std::is_same_v<decltype(std::declval<Tappy<TapMsg2>&>().value_mutable_optional_strong_type()), uint64_t&>);
    REQUIRE(!msg.has_optional_strong_type());
    REQUIRE_THROWS(msg.value_optional_strong_type());
    REQUIRE_THROWS(msg.value_mutable_optional_strong_type());
    SECTION("Modify using set")
    {
      msg.set_optional_strong_type(1234U);
      REQUIRE(msg.has_optional_strong_type());
      REQUIRE(msg.value_optional_strong_type() == 1234U);
      REQUIRE(msg.value_mutable_optional_strong_type() == 1234U);
      SECTION("Modify using value_mutable")
      {
        msg.value_mutable_optional_strong_type() = 2345U;
        REQUIRE(msg.has_optional_strong_type());
        REQUIRE(msg.value_optional_strong_type() == 2345U);
        REQUIRE(msg.value_mutable_optional_strong_type() == 2345U);
      }
      SECTION("Modify using set from optional")
      {
        msg.setopt_optional_strong_type(3456U);
        REQUIRE(msg.has_optional_strong_type());
        REQUIRE(msg.value_optional_strong_type() == 3456U);
        REQUIRE(msg.value_mutable_optional_strong_type() == 3456U);
      }
      SECTION("Clear value")
      {
        REQUIRE(msg.has_optional_strong_type());
        msg.reset_optional_strong_type();
        REQUIRE(!msg.has_optional_strong_type());
      }
      SECTION("Clear from std::optional")
      {
        REQUIRE(msg.has_optional_strong_type());
        msg.setopt_optional_strong_type(std::nullopt);
        REQUIRE(!msg.has_optional_strong_type());
      }
    }
  }
}

TEST_CASE("Static members")
{
  REQUIRE(TapMsg::uuid.to_string() == "cae6ee0b-ec41-40cf-8b87-fb1583e5985d");
}

TEST_CASE("Comparsion operators")
{
  SECTION("Fields")
  {
    Tappy<TapMsg> var_a{};
    auto var_b{var_a};
    REQUIRE(var_a == var_b);

    SECTION("Integral")
    {
      ++var_a.get_mutable_integer();
      REQUIRE(var_a != var_b);
    }
    SECTION("Floating point")
    {
      ++var_a.get_mutable_floating_point();
      REQUIRE(var_a != var_b);
    }
    SECTION("Boolean")
    {
      var_a.set_boolean(true);
      REQUIRE(var_a != var_b);
    }
    SECTION("Array of primitives")
    {
      var_a.get_underlying_array_of_primitives().emplace_back();
      REQUIRE(var_a != var_b);
    }
    SECTION("Array of array")
    {
      var_a.get_underlying_array_of_array().emplace_back();
      REQUIRE(var_a != var_b);
    }
    SECTION("UUID")
    {
      const auto uuid = ::jewels::Uuid<SubMsg>::from_string("11111111111111111111111111111111");
      REQUIRE(uuid);
      var_a.set_uuid(*uuid);
      REQUIRE(var_a != var_b);
    }
    SECTION("UUID different namespace tag")
    {
      const auto uuid = ::jewels::Uuid<AnotherTag>::from_string("11111111111111111111111111111111");
      REQUIRE(uuid);
      var_a.set_uuid_different_namespace(*uuid);
      REQUIRE(var_a != var_b);
    }
    SECTION("Defaulted enum")
    {
      var_a.set_default_enum(SomeEnum::first_value);
      REQUIRE(var_a.get_default_enum() == SomeEnum::first_value);
      var_a.set_default_enum(SomeEnum::second_value);
      REQUIRE(var_a != var_b);
    }
    SECTION("Enum with init")
    {
      REQUIRE(var_a.get_enum_with_init() == SomeEnum::second_value);
      var_a.set_enum_with_init(SomeEnum::first_value);
      REQUIRE(var_a != var_b);
    }
    SECTION("Nested schema")
    {
      var_a.get_mutable_nested_schema().set_field(1L);
      REQUIRE(var_a != var_b);
    }
    SECTION("Array of schema")
    {
      var_a.get_underlying_array_of_schema().emplace_back();
      REQUIRE(var_a != var_b);
    }
    SECTION("Duration")
    {
      REQUIRE(var_a.get_duration().count() == 0L);
      var_a.get_mutable_duration() = std::chrono::nanoseconds{1};
      REQUIRE(var_a != var_b);
    }
    SECTION("Sync time")
    {
      REQUIRE(var_a.get_sync_time().time_since_epoch().count() == 0L);
      var_a.get_mutable_sync_time() = ::jewels::time::SyncTime{std::chrono::nanoseconds{1}};
      REQUIRE(var_a != var_b);
    }
    SECTION("optional")
    {
      REQUIRE(!var_a.has_optional());
      var_a.set_optional(0U);
      REQUIRE(var_a != var_b);
    }
    SECTION("bool init")
    {
      REQUIRE(var_a.get_bool_with_init());
      var_a.set_bool_with_init(false);
      REQUIRE(var_a != var_b);
    }
    SECTION("strong type")
    {
      REQUIRE(var_a.get_strong_type() == 0UL);
      var_a.get_mutable_strong_type() = 1UL;
      REQUIRE(var_a != var_b);
    }
    SECTION("external strong type")
    {
      REQUIRE(var_a.get_external_strong_type() == ::clockwork::external::make(123U));
      var_a.get_mutable_external_strong_type() = ::clockwork::external::make(0U);
      REQUIRE(var_a != var_b);
    }
    SECTION("FixedArray")
    {
      REQUIRE(::std::ranges::all_of(var_a.get_fixed_array(), [](auto value) { return value == 0; }));
      ++::jewels::at(var_a.get_mutable_fixed_array(), 0);
      REQUIRE(var_a != var_b);
    }
  }
  SECTION("With padding")
  {
    // Use tachyon so we can change padding directly.
    Tachyon<PaddedMsg> var_a{};
    auto var_b{var_a};
    REQUIRE(var_a == var_b);

    // Changing padding should not affect the comparison operator.
    ::std::ranges::fill(var_a.padding_0_, std::byte{1});
    REQUIRE(var_a == var_b);
  }
}

TEST_CASE("From buffers")
{
  SECTION("Default constructed")
  {
    Tappy<TapMsg> msg{};
    const auto bytes = read_binary_file("clockwork/dsl/serialization/tests/resources/tapmsg_default.bin");
    REQUIRE(!std::ranges::equal(as_bytes(jewels::as_single_item_span(msg)), as_bytes(std::span{bytes})));
    msg.get_mutable_nested_schema().set_field(0);
    REQUIRE(std::ranges::equal(as_bytes(jewels::as_single_item_span(msg)), as_bytes(std::span{bytes})));
  }
  SECTION("Full")
  {
    Tappy<TapMsg> msg{};
    msg.set_integer(123);
    msg.set_floating_point(9.87f);
    msg.set_boolean(true);
    msg.get_underlying_array_of_primitives() = ::jewels::tap::VarArray<int32_t, 9UL>{0, 1, 2, 3, 4, 5, 6, 7, 8};
    msg.get_underlying_array_of_array() = ::jewels::tap::VarArray<::jewels::tap::VarString<3UL>, 2UL>{
      ::jewels::tap::VarString<3UL>{'a', 'b'}, ::jewels::tap::VarString<3UL>{'c', 'd'}};
    msg.get_mutable_uuid() = *jewels::Uuid<SubMsg>::from_string("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa");
    msg.get_mutable_uuid_different_namespace() =
      *jewels::Uuid<AnotherTag>::from_string("bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb");
    msg.set_default_enum(SomeEnum::second_value);
    msg.set_enum_with_init(SomeEnum::first_value);
    msg.set_default_flags(SomeFlags::flag1 | SomeFlags::flag3);
    msg.set_flags_with_init(SomeFlags::flag1 | SomeFlags::flag2 | SomeFlags::flag3);
    msg.get_mutable_nested_schema().set_field(456);
    Tappy<SubMsg> submsg_a{};
    submsg_a.set_field(1);
    Tappy<SubMsg> submsg_b{};
    submsg_b.set_field(2);
    msg.get_underlying_array_of_schema() = ::jewels::tap::VarArray<Tappy<SubMsg>, 2UL>{submsg_a, submsg_b};
    msg.set_duration(std::chrono::nanoseconds{1212});
    msg.set_sync_time(::jewels::time::SyncTime{std::chrono::nanoseconds{3434}});
    msg.set_optional(789);
    msg.set_bool_with_init(false);
    msg.set_strong_type(9876543210);
    msg.set_external_strong_type(::clockwork::external::make(10101U));
    msg.set_fixed_array(std::array{1, 9});
    msg.get_underlying_var_string() = ::jewels::tap::VarString<2UL>{'+'};

    const auto bytes = read_binary_file("clockwork/dsl/serialization/tests/resources/tapmsg_full.bin");
    REQUIRE(std::ranges::equal(as_bytes(jewels::as_single_item_span(msg)), as_bytes(std::span{bytes})));
  }
  SECTION("Generic values only")
  {
    Tappy<GenericValuesOnly<3UL>> msg{};
    ::std::ranges::copy(::std::ranges::views::iota(1UL, 4UL), std::begin(msg.get_mutable_field()));

    const auto bytes = read_binary_file("clockwork/dsl/serialization/tests/resources/generic_values_only.bin");
    REQUIRE(std::ranges::equal(as_bytes(jewels::as_single_item_span(msg)), as_bytes(std::span{bytes})));
  }
  SECTION("Generic with primitives")
  {
    Tappy<GenericSubMsg<3UL, bool>> msg{};
    msg.get_underlying_field().resize(3);
    ::std::ranges::copy(std::array{true, false, true}, std::begin(msg.get_mutable_field()));

    const auto bytes = read_binary_file("clockwork/dsl/serialization/tests/resources/generic_primitives.bin");
    REQUIRE(std::ranges::equal(as_bytes(jewels::as_single_item_span(msg)), as_bytes(std::span{bytes})));
  }
}

namespace clockwork
{

// This will result in a compilation failure if a TapInit was already defined.
template <>
struct TapInit<Tachyon<NoConstructor>>
{
};

} // namespace clockwork

TEST_CASE("Tap constructor")
{
  using InitT = TapInit<Tachyon<OutOfOrderFields>>;
  using TapT = Tap<Tachyon<OutOfOrderFields>>;
  SECTION("Validate source ordering")
  {
    STATIC_REQUIRE(offsetof(InitT, boolean) == 0);
    STATIC_REQUIRE(offsetof(InitT, integer) == 8);
    STATIC_REQUIRE(offsetof(InitT, floating_point) == 16);
    STATIC_REQUIRE(sizeof(InitT) == 24);
  }
  SECTION("Using init struct")
  {
    const auto tap_from_init = TapT{{
      .boolean = true,
      .integer = 123,
      .floating_point = 4.56f,
    }};
    TapT manual_tap{};
    REQUIRE(manual_tap != tap_from_init);
    manual_tap.set_boolean(true);
    manual_tap.set_integer(123);
    manual_tap.set_floating_point(4.56f);
    REQUIRE(manual_tap == tap_from_init);
  }
  SECTION("Using init via set method")
  {
    Tappy<::clockwork::testing::ConstructorContainer> container;
    container.set_thing({{
      .boolean = true,
      .integer = 123,
      .floating_point = 4.56f,
    }});
    CHECK(container.get_thing().get_boolean() == true);
    CHECK(container.get_thing().get_integer() == 123);
    CHECK(container.get_thing().get_floating_point() == 4.56f);
  }
  SECTION("Validate no constructor provided")
  {
    STATIC_REQUIRE(!std::is_constructible_v<Tappy<NoConstructor>, TapInit<Tachyon<NoConstructor>>>);
  }
}
