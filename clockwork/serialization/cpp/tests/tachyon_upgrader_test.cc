// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/python/python_init.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/serialization/cpp/clk_builtin_type.hh"
#include "clockwork/serialization/cpp/clk_type.hh"
#include "clockwork/serialization/cpp/tachyon_model.hh"
#include "clockwork/serialization/cpp/tachyon_python_upgrader.hh"
#include "clockwork/serialization/cpp/tachyon_upgrader.hh"
#include "clockwork/serialization/cpp/tests/support/test_schema_v1_clk_cc.hh"
#include "clockwork/serialization/cpp/tests/support/test_schema_v2_clk_cc.hh"
#include "clockwork/serialization/cpp/tests/support/validate_upgradability.hh"
#include "clockwork/serialization/metadata/tachyon_model.pb.h"
#include "clockwork/serialization/py/tests/support/simple_schema_v1_clk_cc.hh"
#include "clockwork/serialization/py/tests/support/simple_schema_v2_clk_cc.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/container/compare.hh"
#include "jewels/container/tap/optional.hh"
#include "jewels/container/tap/tensor.hh"
#include "jewels/container/tap/var_array.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/span.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

// IWYU pragma: no_include <__stddef_offsetof.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <memory_resource>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <typeinfo>
#include <variant>

namespace clockwork::serialization
{
namespace
{

/// Create an upgrader that does the upgrade in python
/// @tparam SrcType Source schema type
/// @tparam DestType Destination schema type
template <typename SrcType, typename DestType>
[[nodiscard]] std::unique_ptr<TachyonUpgrader> make_python_upgrader()
{
  metadata::TachyonMetadata dest_proto;
  REQUIRE(dest_proto.ParseFromString(
    std::string{LoggingTraits<DestType>::schema_definition.data(), LoggingTraits<DestType>::schema_definition.size()}));
  dest_proto.set_python_required(true);
  std::string dest_schema_definition;
  REQUIRE(dest_proto.SerializeToString(&dest_schema_definition));
  auto upgrader = make_tachyon_python_upgrader(
    LoggingTraits<DestType>::module_name,
    LoggingTraits<DestType>::source_file_name,
    LoggingTraits<DestType>::class_name,
    std::as_bytes(std::span{dest_schema_definition}),
    std::as_bytes(std::span{LoggingTraits<SrcType>::schema_definition}),
    LoggingTraits<SrcType>::schema_name);
  if (upgrader->upgrade_required())
  {
    REQUIRE(upgrader->upgrader_type() == TachyonUpgraderType::python);
  }
  else
  {
    REQUIRE(upgrader->upgrader_type() == TachyonUpgraderType::memcpy);
  }
  return upgrader;
}

/// Create an upgrader that does the upgrade in C++
/// @tparam SrcType Source schema type
/// @tparam DestType Destination schema type
template <typename SrcType, typename DestType>
[[nodiscard]] std::unique_ptr<TachyonUpgrader> make_cpp_upgrader()
{
  metadata::TachyonMetadata dest_proto;
  REQUIRE(dest_proto.ParseFromString(
    std::string{LoggingTraits<DestType>::schema_definition.data(), LoggingTraits<DestType>::schema_definition.size()}));
  dest_proto.set_python_required(false);
  std::string dest_schema_definition;
  REQUIRE(dest_proto.SerializeToString(&dest_schema_definition));
  auto upgrader = make_tachyon_cpp_upgrader(
    LoggingTraits<DestType>::class_name,
    std::as_bytes(std::span{dest_schema_definition}),
    std::as_bytes(std::span{LoggingTraits<SrcType>::schema_definition}));
  if (upgrader->upgrade_required())
  {
    REQUIRE(upgrader->upgrader_type() == TachyonUpgraderType::cpp);
  }
  else
  {
    REQUIRE(upgrader->upgrader_type() == TachyonUpgraderType::memcpy);
  }
  return upgrader;
}

TEST_CASE("Schemas are incompatible")
{
  REQUIRE_THROWS(
    make_tachyon_cpp_upgrader<Tappy<tests::SimpleSchemaV1>>(
      std::as_bytes(std::span{LoggingTraits<Tappy<tests::SimpleSchemaV2>>::schema_definition})));
}

TEST_CASE("Schemas are compatible, no upgrade needed")
{
  const auto upgrader = make_tachyon_cpp_upgrader<Tappy<tests::SimpleSchemaV2>>(
    std::as_bytes(std::span{LoggingTraits<Tappy<tests::SimpleSchemaV2>>::schema_definition}));
  REQUIRE_FALSE(upgrader->upgrade_required());

  Tappy<tests::SimpleSchemaV2> input_instance{};
  input_instance.set_integer_field(42);
  REQUIRE(input_instance.get_underlying_string_field().try_set("test"));
  Tappy<tests::SimpleSchemaV2> output_instance{};

  upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&output_instance, 1U}));
  REQUIRE(input_instance.get_integer_field() == output_instance.get_integer_field());
  REQUIRE(input_instance.get_string_field() == output_instance.get_string_field());
}

TEST_CASE("Schemas are compatible, upgrade needed")
{
  const auto upgrader = make_tachyon_cpp_upgrader<Tappy<tests::SimpleSchemaV2>>(
    std::as_bytes(std::span{LoggingTraits<Tappy<tests::SimpleSchemaV1>>::schema_definition}));
  REQUIRE(upgrader->upgrade_required());

  Tappy<tests::SimpleSchemaV1> input_instance{};
  input_instance.set_integer_field(42);
  Tappy<tests::SimpleSchemaV2> output_instance{};

  upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&output_instance, 1U}));
  REQUIRE(input_instance.get_integer_field() == output_instance.get_integer_field());
  REQUIRE(output_instance.get_string_field().empty());
}

TEST_CASE("var_array_size_offset")
{
  static constexpr size_t array_size = 7U; // Prime to probe all possible padding sizes
  using Layout1 = jewels::tap::detail::VarArrayLayout<std::array<char, 1U>, array_size>;
  REQUIRE(offsetof(Layout1, size) == var_array_size_offset(array_size, 1U));
  using Layout2 = jewels::tap::detail::VarArrayLayout<std::array<char, 2U>, array_size>;
  REQUIRE(offsetof(Layout2, size) == var_array_size_offset(array_size, 2U));
  using Layout3 = jewels::tap::detail::VarArrayLayout<std::array<char, 3U>, array_size>;
  REQUIRE(offsetof(Layout3, size) == var_array_size_offset(array_size, 3U));
  using Layout4 = jewels::tap::detail::VarArrayLayout<std::array<char, 4U>, array_size>;
  REQUIRE(offsetof(Layout4, size) == var_array_size_offset(array_size, 4U));
  using Layout5 = jewels::tap::detail::VarArrayLayout<std::array<char, 5U>, array_size>;
  REQUIRE(offsetof(Layout5, size) == var_array_size_offset(array_size, 5U));
  using Layout6 = jewels::tap::detail::VarArrayLayout<std::array<char, 6U>, array_size>;
  REQUIRE(offsetof(Layout6, size) == var_array_size_offset(array_size, 6U));
  using Layout7 = jewels::tap::detail::VarArrayLayout<std::array<char, 7U>, array_size>;
  REQUIRE(offsetof(Layout7, size) == var_array_size_offset(array_size, 7U));
  using Layout8 = jewels::tap::detail::VarArrayLayout<std::array<char, 8U>, array_size>;
  REQUIRE(offsetof(Layout8, size) == var_array_size_offset(array_size, 8U));
}

TEST_CASE("optional_has_value_offset")
{
  using Layout1 = jewels::tap::detail::OptionalLayout<std::array<char, 1U>>;
  REQUIRE(offsetof(Layout1, has_value) == optional_has_value_offset(1U));
  using Layout2 = jewels::tap::detail::OptionalLayout<std::array<char, 2U>>;
  REQUIRE(offsetof(Layout2, has_value) == optional_has_value_offset(2U));
  using Layout3 = jewels::tap::detail::OptionalLayout<std::array<char, 3U>>;
  REQUIRE(offsetof(Layout3, has_value) == optional_has_value_offset(3U));
  using Layout4 = jewels::tap::detail::OptionalLayout<std::array<char, 4U>>;
  REQUIRE(offsetof(Layout4, has_value) == optional_has_value_offset(4U));
  using Layout5 = jewels::tap::detail::OptionalLayout<std::array<char, 5U>>;
  REQUIRE(offsetof(Layout5, has_value) == optional_has_value_offset(5U));
  using Layout6 = jewels::tap::detail::OptionalLayout<std::array<char, 6U>>;
  REQUIRE(offsetof(Layout6, has_value) == optional_has_value_offset(6U));
  using Layout7 = jewels::tap::detail::OptionalLayout<std::array<char, 7U>>;
  REQUIRE(offsetof(Layout7, has_value) == optional_has_value_offset(7U));
  using Layout8 = jewels::tap::detail::OptionalLayout<std::array<char, 8U>>;
  REQUIRE(offsetof(Layout8, has_value) == optional_has_value_offset(8U));
}

TEST_CASE("load/store_soa_size")
{
  struct __attribute__((packed)) TestType
  {
    uint64_t size8{};
    uint32_t size4{};
    uint16_t size2{};
    uint8_t size1{};
  } test_struct{};
  const auto test_span = std::as_writable_bytes(jewels::as_single_item_span(test_struct));

  REQUIRE_THROWS(load_soa_size(test_span, 0U, 0U));
  REQUIRE_THROWS(load_soa_size(test_span, 0U, 5U));

  REQUIRE(load_soa_size(test_span, 0U, sizeof(uint64_t)) == 0U);
  store_soa_size(test_span, 0U, sizeof(uint64_t), 0x0123456789abcdefU);
  REQUIRE(load_soa_size(test_span, 0U, sizeof(uint64_t)) == 0x0123456789abcdefU);

  REQUIRE(load_soa_size(test_span, sizeof(uint64_t), sizeof(uint32_t)) == 0U);
  store_soa_size(test_span, sizeof(uint64_t), sizeof(uint32_t), 0x01234567U);
  REQUIRE(load_soa_size(test_span, sizeof(uint64_t), sizeof(uint32_t)) == 0x01234567U);

  REQUIRE(load_soa_size(test_span, sizeof(uint64_t) + sizeof(uint32_t), sizeof(uint16_t)) == 0U);
  store_soa_size(test_span, sizeof(uint64_t) + sizeof(uint32_t), sizeof(uint16_t), 0x0123U);
  REQUIRE(load_soa_size(test_span, sizeof(uint64_t) + sizeof(uint32_t), sizeof(uint16_t)) == 0x0123U);

  REQUIRE(load_soa_size(test_span, sizeof(uint64_t) + sizeof(uint32_t) + sizeof(uint16_t), sizeof(uint8_t)) == 0U);
  store_soa_size(test_span, sizeof(uint64_t) + sizeof(uint32_t) + sizeof(uint16_t), sizeof(uint8_t), 0x01U);
  REQUIRE(load_soa_size(test_span, sizeof(uint64_t) + sizeof(uint32_t) + sizeof(uint16_t), sizeof(uint8_t)) == 0x01U);
}

TEST_CASE("Smoke test")
{
  // Initialize python for the log schema upgrader
  python::python_init(python::InitializationMode::unit_test);

  const auto python_upgrader = make_python_upgrader<Tappy<tests::SmokeTestSchemaV1>, Tappy<tests::SmokeTestSchemaV2>>();
  REQUIRE(python_upgrader->upgrade_required());

  const auto cpp_upgrader = make_cpp_upgrader<Tappy<tests::SmokeTestSchemaV1>, Tappy<tests::SmokeTestSchemaV2>>();
  REQUIRE(cpp_upgrader->upgrade_required());

  validate_upgradability<Tappy<tests::SmokeTestSchemaV1>, Tappy<tests::SmokeTestSchemaV2>>();

  Tappy<tests::SmokeTestSchemaV1> input_instance{};
  input_instance.set_int8_field(42);
  Tappy<tests::SmokeTestSchemaV2> python_instance{};
  Tappy<tests::SmokeTestSchemaV2> cpp_instance{};
  std::memset(&python_instance, 0, sizeof(python_instance));
  std::memset(&cpp_instance, 0, sizeof(cpp_instance));

  python_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&python_instance, 1U}));
  cpp_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&cpp_instance, 1U}));

  const auto check_python = GENERATE(true, false);
  CAPTURE(check_python);
  const auto checked_instance = check_python ? python_instance : cpp_instance;

  REQUIRE(input_instance.get_int8_field() == checked_instance.get_int8_field());
  REQUIRE(checked_instance.get_uint32_field() == 0U);

  REQUIRE(
    std::ranges::equal(std::as_bytes(std::span{&cpp_instance, 1U}), std::as_bytes(std::span{&python_instance, 1U})));
}

TEST_CASE("Parameterized upgrade test V1 -> V1")
{
  auto cpp_upgrader = make_cpp_upgrader<Tappy<tests::ParameterizedSchemaV1_3>, Tappy<tests::ParameterizedSchemaV1_3>>();
  REQUIRE_FALSE(cpp_upgrader->upgrade_required());

  cpp_upgrader = make_cpp_upgrader<Tappy<tests::ParameterizedSchemaV1_3>, Tappy<tests::ParameterizedSchemaV1_4>>();
  REQUIRE(cpp_upgrader->upgrade_required());

  validate_upgradability<Tappy<tests::ParameterizedSchemaV1_3>, Tappy<tests::ParameterizedSchemaV1_4>>(
    UpgradeValidationOption::upgrade_and_downgrade);

  Tappy<tests::ParameterizedSchemaV1_3> input_instance{};
  auto& element0 = input_instance.get_underlying_vararray_field().emplace_back();
  element0.set_int8_field(1);
  auto& element1 = input_instance.get_underlying_vararray_field().emplace_back();
  element1.set_int8_field(2);
  auto& element2 = input_instance.get_underlying_vararray_field().emplace_back();
  element2.set_int8_field(3);
  Tappy<tests::ParameterizedSchemaV1_4> cpp_instance{};
  std::memset(&cpp_instance, 0, sizeof(cpp_instance));

  cpp_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&cpp_instance, 1U}));

  REQUIRE(cpp_instance.get_vararray_field().size() == 3U);
  REQUIRE(cpp_instance.get_vararray_field()[0U] == element0);
  REQUIRE(cpp_instance.get_vararray_field()[1U] == element1);
  REQUIRE(cpp_instance.get_vararray_field()[2U] == element2);
}

TEST_CASE("Parameterized upgrade test V1 -> V2 (no size change)")
{
  const auto cpp_upgrader =
    make_cpp_upgrader<Tappy<tests::ParameterizedSchemaV1_3>, Tappy<tests::ParameterizedSchemaV2_3>>();
  REQUIRE(cpp_upgrader->upgrade_required());

  validate_upgradability<Tappy<tests::ParameterizedSchemaV1_3>, Tappy<tests::ParameterizedSchemaV2_3>>();

  Tappy<tests::ParameterizedSchemaV1_3> input_instance{};
  auto& element0 = input_instance.get_underlying_vararray_field().emplace_back();
  element0.set_int8_field(1);
  auto& element1 = input_instance.get_underlying_vararray_field().emplace_back();
  element1.set_int8_field(2);
  auto& element2 = input_instance.get_underlying_vararray_field().emplace_back();
  element2.set_int8_field(3);
  Tappy<tests::ParameterizedSchemaV2_3> cpp_instance{};
  std::memset(&cpp_instance, 0, sizeof(cpp_instance));

  cpp_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&cpp_instance, 1U}));

  REQUIRE(cpp_instance.get_vararray_field().size() == 3U);
  REQUIRE(cpp_instance.get_vararray_field()[0U].get_int8_field() == element0.get_int8_field());
  REQUIRE(cpp_instance.get_vararray_field()[0U].get_uint32_field() == 0U);
  REQUIRE(cpp_instance.get_vararray_field()[1U].get_int8_field() == element1.get_int8_field());
  REQUIRE(cpp_instance.get_vararray_field()[1U].get_uint32_field() == 0U);
  REQUIRE(cpp_instance.get_vararray_field()[2U].get_int8_field() == element2.get_int8_field());
  REQUIRE(cpp_instance.get_vararray_field()[2U].get_uint32_field() == 0U);
}

TEST_CASE("Parameterized upgrade test V1 -> V2 (size change)")
{
  const auto cpp_upgrader =
    make_cpp_upgrader<Tappy<tests::ParameterizedSchemaV1_3>, Tappy<tests::ParameterizedSchemaV2_4>>();
  REQUIRE(cpp_upgrader->upgrade_required());

  validate_upgradability<Tappy<tests::ParameterizedSchemaV1_3>, Tappy<tests::ParameterizedSchemaV2_4>>();

  Tappy<tests::ParameterizedSchemaV1_3> input_instance{};
  auto& element0 = input_instance.get_underlying_vararray_field().emplace_back();
  element0.set_int8_field(1);
  auto& element1 = input_instance.get_underlying_vararray_field().emplace_back();
  element1.set_int8_field(2);
  auto& element2 = input_instance.get_underlying_vararray_field().emplace_back();
  element2.set_int8_field(3);
  Tappy<tests::ParameterizedSchemaV2_4> cpp_instance{};
  std::memset(&cpp_instance, 0, sizeof(cpp_instance));

  cpp_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&cpp_instance, 1U}));

  REQUIRE(cpp_instance.get_vararray_field().size() == 3U);
  REQUIRE(cpp_instance.get_vararray_field()[0U].get_int8_field() == element0.get_int8_field());
  REQUIRE(cpp_instance.get_vararray_field()[0U].get_uint32_field() == 0U);
  REQUIRE(cpp_instance.get_vararray_field()[1U].get_int8_field() == element1.get_int8_field());
  REQUIRE(cpp_instance.get_vararray_field()[1U].get_uint32_field() == 0U);
  REQUIRE(cpp_instance.get_vararray_field()[2U].get_int8_field() == element2.get_int8_field());
  REQUIRE(cpp_instance.get_vararray_field()[2U].get_uint32_field() == 0U);
}

TEST_CASE("Int8 upgrade")
{
  // Initialize python for the log schema upgrader
  python::python_init(python::InitializationMode::unit_test);

  const auto python_upgrader =
    make_python_upgrader<Tappy<tests::Int8UpgradeTestSchemaV1>, Tappy<tests::Int8UpgradeTestSchemaV2>>();
  REQUIRE(python_upgrader->upgrade_required());

  const auto cpp_upgrader =
    make_cpp_upgrader<Tappy<tests::Int8UpgradeTestSchemaV1>, Tappy<tests::Int8UpgradeTestSchemaV2>>();
  REQUIRE(cpp_upgrader->upgrade_required());

  validate_upgradability<Tappy<tests::Int8UpgradeTestSchemaV1>, Tappy<tests::Int8UpgradeTestSchemaV2>>();

  Tappy<tests::Int8UpgradeTestSchemaV1> input_instance{};
  input_instance.set_int8_field(-11);
  input_instance.set_int16_field(-12);
  input_instance.set_int32_field(-13);
  input_instance.set_int64_field(-14);
  input_instance.set_uint8_field(21);
  input_instance.set_uint16_field(22);
  input_instance.set_uint32_field(23);
  input_instance.set_uint64_field(24);
  input_instance.set_float32_field(25);
  input_instance.set_float64_field(26);
  Tappy<tests::Int8UpgradeTestSchemaV2> python_instance{};
  Tappy<tests::Int8UpgradeTestSchemaV2> cpp_instance{};
  std::memset(&python_instance, 0, sizeof(python_instance));
  std::memset(&cpp_instance, 0, sizeof(cpp_instance));

  python_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&python_instance, 1U}));
  cpp_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&cpp_instance, 1U}));

  const auto check_python = GENERATE(true, false);
  CAPTURE(check_python);
  const auto checked_instance = check_python ? python_instance : cpp_instance;

  REQUIRE(checked_instance.get_int8_field() == input_instance.get_int8_field());
  REQUIRE(checked_instance.get_int16_field() == static_cast<int16_t>(input_instance.get_int16_field()));
  REQUIRE(checked_instance.get_int32_field() == static_cast<int32_t>(input_instance.get_int32_field()));
  REQUIRE(checked_instance.get_int64_field() == static_cast<int64_t>(input_instance.get_int64_field()));
  REQUIRE(checked_instance.get_uint8_field() == static_cast<uint8_t>(input_instance.get_uint8_field()));
  REQUIRE(checked_instance.get_uint16_field() == static_cast<uint16_t>(input_instance.get_uint16_field()));
  REQUIRE(checked_instance.get_uint32_field() == static_cast<uint32_t>(input_instance.get_uint32_field()));
  REQUIRE(checked_instance.get_uint64_field() == static_cast<uint64_t>(input_instance.get_uint64_field()));
  REQUIRE(checked_instance.get_float32_field() == static_cast<float>(input_instance.get_float32_field()));
  REQUIRE(checked_instance.get_float64_field() == static_cast<double>(input_instance.get_float64_field()));
  REQUIRE(checked_instance.get_new_field() == -42);

  REQUIRE(
    std::ranges::equal(std::as_bytes(std::span{&cpp_instance, 1U}), std::as_bytes(std::span{&python_instance, 1U})));
}

TEST_CASE("Int16 upgrade")
{
  // Initialize python for the log schema upgrader
  python::python_init(python::InitializationMode::unit_test);

  const auto python_upgrader =
    make_python_upgrader<Tappy<tests::Int16UpgradeTestSchemaV1>, Tappy<tests::Int16UpgradeTestSchemaV2>>();
  REQUIRE(python_upgrader->upgrade_required());

  const auto cpp_upgrader =
    make_cpp_upgrader<Tappy<tests::Int16UpgradeTestSchemaV1>, Tappy<tests::Int16UpgradeTestSchemaV2>>();
  REQUIRE(cpp_upgrader->upgrade_required());

  validate_upgradability<Tappy<tests::Int16UpgradeTestSchemaV1>, Tappy<tests::Int16UpgradeTestSchemaV2>>();

  Tappy<tests::Int16UpgradeTestSchemaV1> input_instance{};
  input_instance.set_int8_field(-11);
  input_instance.set_int16_field(-12);
  input_instance.set_int32_field(-13);
  input_instance.set_int64_field(-14);
  input_instance.set_uint8_field(21);
  input_instance.set_uint16_field(22);
  input_instance.set_uint32_field(23);
  input_instance.set_uint64_field(24);
  input_instance.set_float32_field(25);
  input_instance.set_float64_field(26);
  Tappy<tests::Int16UpgradeTestSchemaV2> python_instance{};
  Tappy<tests::Int16UpgradeTestSchemaV2> cpp_instance{};
  std::memset(&python_instance, 0, sizeof(python_instance));
  std::memset(&cpp_instance, 0, sizeof(cpp_instance));

  python_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&python_instance, 1U}));
  cpp_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&cpp_instance, 1U}));

  const auto check_python = GENERATE(true, false);
  CAPTURE(check_python);
  const auto checked_instance = check_python ? python_instance : cpp_instance;

  REQUIRE(checked_instance.get_int8_field() == static_cast<int8_t>(input_instance.get_int8_field()));
  REQUIRE(checked_instance.get_int16_field() == input_instance.get_int16_field());
  REQUIRE(checked_instance.get_int32_field() == static_cast<int32_t>(input_instance.get_int32_field()));
  REQUIRE(checked_instance.get_int64_field() == static_cast<int64_t>(input_instance.get_int64_field()));
  REQUIRE(checked_instance.get_uint8_field() == static_cast<uint8_t>(input_instance.get_uint8_field()));
  REQUIRE(checked_instance.get_uint16_field() == static_cast<uint16_t>(input_instance.get_uint16_field()));
  REQUIRE(checked_instance.get_uint32_field() == static_cast<uint32_t>(input_instance.get_uint32_field()));
  REQUIRE(checked_instance.get_uint64_field() == static_cast<uint64_t>(input_instance.get_uint64_field()));
  REQUIRE(checked_instance.get_float32_field() == static_cast<float>(input_instance.get_float32_field()));
  REQUIRE(checked_instance.get_float64_field() == static_cast<double>(input_instance.get_float64_field()));
  REQUIRE(checked_instance.get_new_field() == -42);

  REQUIRE(
    std::ranges::equal(std::as_bytes(std::span{&cpp_instance, 1U}), std::as_bytes(std::span{&python_instance, 1U})));
}

TEST_CASE("Int32 upgrade")
{
  // Initialize python for the log schema upgrader
  python::python_init(python::InitializationMode::unit_test);

  const auto python_upgrader =
    make_python_upgrader<Tappy<tests::Int32UpgradeTestSchemaV1>, Tappy<tests::Int32UpgradeTestSchemaV2>>();
  REQUIRE(python_upgrader->upgrade_required());

  const auto cpp_upgrader =
    make_cpp_upgrader<Tappy<tests::Int32UpgradeTestSchemaV1>, Tappy<tests::Int32UpgradeTestSchemaV2>>();
  REQUIRE(cpp_upgrader->upgrade_required());

  validate_upgradability<Tappy<tests::Int32UpgradeTestSchemaV1>, Tappy<tests::Int32UpgradeTestSchemaV2>>();

  Tappy<tests::Int32UpgradeTestSchemaV1> input_instance{};
  input_instance.set_int8_field(-11);
  input_instance.set_int16_field(-12);
  input_instance.set_int32_field(-13);
  input_instance.set_int64_field(-14);
  input_instance.set_uint8_field(21);
  input_instance.set_uint16_field(22);
  input_instance.set_uint32_field(23);
  input_instance.set_uint64_field(24);
  input_instance.set_float32_field(25);
  input_instance.set_float64_field(26);
  Tappy<tests::Int32UpgradeTestSchemaV2> python_instance{};
  Tappy<tests::Int32UpgradeTestSchemaV2> cpp_instance{};
  std::memset(&python_instance, 0, sizeof(python_instance));
  std::memset(&cpp_instance, 0, sizeof(cpp_instance));

  python_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&python_instance, 1U}));
  cpp_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&cpp_instance, 1U}));

  const auto check_python = GENERATE(true, false);
  CAPTURE(check_python);
  const auto checked_instance = check_python ? python_instance : cpp_instance;

  REQUIRE(checked_instance.get_int8_field() == static_cast<int8_t>(input_instance.get_int8_field()));
  REQUIRE(checked_instance.get_int16_field() == static_cast<int16_t>(input_instance.get_int16_field()));
  REQUIRE(checked_instance.get_int32_field() == input_instance.get_int32_field());
  REQUIRE(checked_instance.get_int64_field() == static_cast<int64_t>(input_instance.get_int64_field()));
  REQUIRE(checked_instance.get_uint8_field() == static_cast<uint8_t>(input_instance.get_uint8_field()));
  REQUIRE(checked_instance.get_uint16_field() == static_cast<uint16_t>(input_instance.get_uint16_field()));
  REQUIRE(checked_instance.get_uint32_field() == static_cast<uint32_t>(input_instance.get_uint32_field()));
  REQUIRE(checked_instance.get_uint64_field() == static_cast<uint64_t>(input_instance.get_uint64_field()));
  REQUIRE(checked_instance.get_float32_field() == static_cast<float>(input_instance.get_float32_field()));
  REQUIRE(checked_instance.get_float64_field() == static_cast<double>(input_instance.get_float64_field()));
  REQUIRE(checked_instance.get_new_field() == -42);

  REQUIRE(
    std::ranges::equal(std::as_bytes(std::span{&cpp_instance, 1U}), std::as_bytes(std::span{&python_instance, 1U})));
}

TEST_CASE("Int64 upgrade")
{
  // Initialize python for the log schema upgrader
  python::python_init(python::InitializationMode::unit_test);

  const auto python_upgrader =
    make_python_upgrader<Tappy<tests::Int64UpgradeTestSchemaV1>, Tappy<tests::Int64UpgradeTestSchemaV2>>();
  REQUIRE(python_upgrader->upgrade_required());

  const auto cpp_upgrader =
    make_cpp_upgrader<Tappy<tests::Int64UpgradeTestSchemaV1>, Tappy<tests::Int64UpgradeTestSchemaV2>>();
  REQUIRE(cpp_upgrader->upgrade_required());

  validate_upgradability<Tappy<tests::Int64UpgradeTestSchemaV1>, Tappy<tests::Int64UpgradeTestSchemaV2>>();

  Tappy<tests::Int64UpgradeTestSchemaV1> input_instance{};
  input_instance.set_int8_field(-11);
  input_instance.set_int16_field(-12);
  input_instance.set_int32_field(-13);
  input_instance.set_int64_field(-14);
  input_instance.set_uint8_field(21);
  input_instance.set_uint16_field(22);
  input_instance.set_uint32_field(23);
  input_instance.set_uint64_field(24);
  input_instance.set_float32_field(25);
  input_instance.set_float64_field(26);
  Tappy<tests::Int64UpgradeTestSchemaV2> python_instance{};
  Tappy<tests::Int64UpgradeTestSchemaV2> cpp_instance{};
  std::memset(&python_instance, 0, sizeof(python_instance));
  std::memset(&cpp_instance, 0, sizeof(cpp_instance));

  python_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&python_instance, 1U}));
  cpp_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&cpp_instance, 1U}));

  const auto check_python = GENERATE(true, false);
  CAPTURE(check_python);
  const auto checked_instance = check_python ? python_instance : cpp_instance;

  REQUIRE(checked_instance.get_int8_field() == static_cast<int8_t>(input_instance.get_int8_field()));
  REQUIRE(checked_instance.get_int16_field() == static_cast<int16_t>(input_instance.get_int16_field()));
  REQUIRE(checked_instance.get_int32_field() == static_cast<int32_t>(input_instance.get_int32_field()));
  REQUIRE(checked_instance.get_int64_field() == input_instance.get_int64_field());
  REQUIRE(checked_instance.get_uint8_field() == static_cast<uint8_t>(input_instance.get_uint8_field()));
  REQUIRE(checked_instance.get_uint16_field() == static_cast<uint16_t>(input_instance.get_uint16_field()));
  REQUIRE(checked_instance.get_uint32_field() == static_cast<uint32_t>(input_instance.get_uint32_field()));
  REQUIRE(checked_instance.get_uint64_field() == static_cast<uint64_t>(input_instance.get_uint64_field()));
  REQUIRE(checked_instance.get_float32_field() == static_cast<float>(input_instance.get_float32_field()));
  REQUIRE(checked_instance.get_float64_field() == static_cast<double>(input_instance.get_float64_field()));
  REQUIRE(checked_instance.get_new_field() == -42);

  REQUIRE(
    std::ranges::equal(std::as_bytes(std::span{&cpp_instance, 1U}), std::as_bytes(std::span{&python_instance, 1U})));
}

TEST_CASE("UInt8 upgrade")
{
  // Initialize python for the log schema upgrader
  python::python_init(python::InitializationMode::unit_test);

  const auto python_upgrader =
    make_python_upgrader<Tappy<tests::UInt8UpgradeTestSchemaV1>, Tappy<tests::UInt8UpgradeTestSchemaV2>>();
  REQUIRE(python_upgrader->upgrade_required());

  const auto cpp_upgrader =
    make_cpp_upgrader<Tappy<tests::UInt8UpgradeTestSchemaV1>, Tappy<tests::UInt8UpgradeTestSchemaV2>>();
  REQUIRE(cpp_upgrader->upgrade_required());

  validate_upgradability<Tappy<tests::UInt8UpgradeTestSchemaV1>, Tappy<tests::UInt8UpgradeTestSchemaV2>>();

  Tappy<tests::UInt8UpgradeTestSchemaV1> input_instance{};
  input_instance.set_int8_field(11);
  input_instance.set_int16_field(12);
  input_instance.set_int32_field(13);
  input_instance.set_int64_field(14);
  input_instance.set_uint8_field(21);
  input_instance.set_uint16_field(22);
  input_instance.set_uint32_field(23);
  input_instance.set_uint64_field(24);
  input_instance.set_float32_field(25);
  input_instance.set_float64_field(26);
  Tappy<tests::UInt8UpgradeTestSchemaV2> python_instance{};
  Tappy<tests::UInt8UpgradeTestSchemaV2> cpp_instance{};
  std::memset(&python_instance, 0, sizeof(python_instance));
  std::memset(&cpp_instance, 0, sizeof(cpp_instance));

  python_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&python_instance, 1U}));
  cpp_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&cpp_instance, 1U}));

  const auto check_python = GENERATE(true, false);
  CAPTURE(check_python);
  const auto checked_instance = check_python ? python_instance : cpp_instance;

  REQUIRE(checked_instance.get_int8_field() == static_cast<int8_t>(input_instance.get_int8_field()));
  REQUIRE(checked_instance.get_int16_field() == static_cast<int16_t>(input_instance.get_int16_field()));
  REQUIRE(checked_instance.get_int32_field() == static_cast<int32_t>(input_instance.get_int32_field()));
  REQUIRE(checked_instance.get_int64_field() == static_cast<int64_t>(input_instance.get_int64_field()));
  REQUIRE(checked_instance.get_uint8_field() == input_instance.get_uint8_field());
  REQUIRE(checked_instance.get_uint16_field() == static_cast<uint16_t>(input_instance.get_uint16_field()));
  REQUIRE(checked_instance.get_uint32_field() == static_cast<uint32_t>(input_instance.get_uint32_field()));
  REQUIRE(checked_instance.get_uint64_field() == static_cast<uint64_t>(input_instance.get_uint64_field()));
  REQUIRE(checked_instance.get_float32_field() == static_cast<float>(input_instance.get_float32_field()));
  REQUIRE(checked_instance.get_float64_field() == static_cast<double>(input_instance.get_float64_field()));
  REQUIRE(checked_instance.get_new_field() == 42);

  REQUIRE(
    std::ranges::equal(std::as_bytes(std::span{&cpp_instance, 1U}), std::as_bytes(std::span{&python_instance, 1U})));
}

TEST_CASE("UInt16 upgrade")
{
  // Initialize python for the log schema upgrader
  python::python_init(python::InitializationMode::unit_test);

  const auto python_upgrader =
    make_python_upgrader<Tappy<tests::UInt16UpgradeTestSchemaV1>, Tappy<tests::UInt16UpgradeTestSchemaV2>>();
  REQUIRE(python_upgrader->upgrade_required());

  const auto cpp_upgrader =
    make_cpp_upgrader<Tappy<tests::UInt16UpgradeTestSchemaV1>, Tappy<tests::UInt16UpgradeTestSchemaV2>>();
  REQUIRE(cpp_upgrader->upgrade_required());

  validate_upgradability<Tappy<tests::UInt16UpgradeTestSchemaV1>, Tappy<tests::UInt16UpgradeTestSchemaV2>>();

  Tappy<tests::UInt16UpgradeTestSchemaV1> input_instance{};
  input_instance.set_int8_field(11);
  input_instance.set_int16_field(12);
  input_instance.set_int32_field(13);
  input_instance.set_int64_field(14);
  input_instance.set_uint8_field(21);
  input_instance.set_uint16_field(22);
  input_instance.set_uint32_field(23);
  input_instance.set_uint64_field(24);
  input_instance.set_float32_field(25);
  input_instance.set_float64_field(26);
  Tappy<tests::UInt16UpgradeTestSchemaV2> python_instance{};
  Tappy<tests::UInt16UpgradeTestSchemaV2> cpp_instance{};
  std::memset(&python_instance, 0, sizeof(python_instance));
  std::memset(&cpp_instance, 0, sizeof(cpp_instance));

  python_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&python_instance, 1U}));
  cpp_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&cpp_instance, 1U}));

  const auto check_python = GENERATE(true, false);
  CAPTURE(check_python);
  const auto checked_instance = check_python ? python_instance : cpp_instance;

  REQUIRE(checked_instance.get_int8_field() == static_cast<int8_t>(input_instance.get_int8_field()));
  REQUIRE(checked_instance.get_int16_field() == static_cast<int16_t>(input_instance.get_int16_field()));
  REQUIRE(checked_instance.get_int32_field() == static_cast<int32_t>(input_instance.get_int32_field()));
  REQUIRE(checked_instance.get_int64_field() == static_cast<int64_t>(input_instance.get_int64_field()));
  REQUIRE(checked_instance.get_uint8_field() == static_cast<uint8_t>(input_instance.get_uint8_field()));
  REQUIRE(checked_instance.get_uint16_field() == input_instance.get_uint16_field());
  REQUIRE(checked_instance.get_uint32_field() == static_cast<uint32_t>(input_instance.get_uint32_field()));
  REQUIRE(checked_instance.get_uint64_field() == static_cast<uint64_t>(input_instance.get_uint64_field()));
  REQUIRE(checked_instance.get_float32_field() == static_cast<float>(input_instance.get_float32_field()));
  REQUIRE(checked_instance.get_float64_field() == static_cast<double>(input_instance.get_float64_field()));
  REQUIRE(checked_instance.get_new_field() == 42);

  REQUIRE(
    std::ranges::equal(std::as_bytes(std::span{&cpp_instance, 1U}), std::as_bytes(std::span{&python_instance, 1U})));
}

TEST_CASE("UInt32 upgrade")
{
  // Initialize python for the log schema upgrader
  python::python_init(python::InitializationMode::unit_test);

  const auto python_upgrader =
    make_python_upgrader<Tappy<tests::UInt32UpgradeTestSchemaV1>, Tappy<tests::UInt32UpgradeTestSchemaV2>>();
  REQUIRE(python_upgrader->upgrade_required());

  const auto cpp_upgrader =
    make_cpp_upgrader<Tappy<tests::UInt32UpgradeTestSchemaV1>, Tappy<tests::UInt32UpgradeTestSchemaV2>>();
  REQUIRE(cpp_upgrader->upgrade_required());

  validate_upgradability<Tappy<tests::UInt32UpgradeTestSchemaV1>, Tappy<tests::UInt32UpgradeTestSchemaV2>>();

  Tappy<tests::UInt32UpgradeTestSchemaV1> input_instance{};
  input_instance.set_int8_field(11);
  input_instance.set_int16_field(12);
  input_instance.set_int32_field(13);
  input_instance.set_int64_field(14);
  input_instance.set_uint8_field(21);
  input_instance.set_uint16_field(22);
  input_instance.set_uint32_field(23);
  input_instance.set_uint64_field(24);
  input_instance.set_float32_field(25);
  input_instance.set_float64_field(26);
  Tappy<tests::UInt32UpgradeTestSchemaV2> python_instance{};
  Tappy<tests::UInt32UpgradeTestSchemaV2> cpp_instance{};
  std::memset(&python_instance, 0, sizeof(python_instance));
  std::memset(&cpp_instance, 0, sizeof(cpp_instance));

  python_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&python_instance, 1U}));
  cpp_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&cpp_instance, 1U}));

  const auto check_python = GENERATE(true, false);
  CAPTURE(check_python);
  const auto checked_instance = check_python ? python_instance : cpp_instance;

  REQUIRE(checked_instance.get_int8_field() == static_cast<int8_t>(input_instance.get_int8_field()));
  REQUIRE(checked_instance.get_int16_field() == static_cast<int16_t>(input_instance.get_int16_field()));
  REQUIRE(checked_instance.get_int32_field() == static_cast<int32_t>(input_instance.get_int32_field()));
  REQUIRE(checked_instance.get_int64_field() == static_cast<int64_t>(input_instance.get_int64_field()));
  REQUIRE(checked_instance.get_uint8_field() == static_cast<uint8_t>(input_instance.get_uint8_field()));
  REQUIRE(checked_instance.get_uint16_field() == static_cast<uint16_t>(input_instance.get_uint16_field()));
  REQUIRE(checked_instance.get_uint32_field() == input_instance.get_uint32_field());
  REQUIRE(checked_instance.get_uint64_field() == static_cast<uint64_t>(input_instance.get_uint64_field()));
  REQUIRE(checked_instance.get_float32_field() == static_cast<float>(input_instance.get_float32_field()));
  REQUIRE(checked_instance.get_float64_field() == static_cast<double>(input_instance.get_float64_field()));
  REQUIRE(checked_instance.get_new_field() == 42);

  REQUIRE(
    std::ranges::equal(std::as_bytes(std::span{&cpp_instance, 1U}), std::as_bytes(std::span{&python_instance, 1U})));
}

TEST_CASE("UInt64 upgrade")
{
  // Initialize python for the log schema upgrader
  python::python_init(python::InitializationMode::unit_test);

  const auto python_upgrader =
    make_python_upgrader<Tappy<tests::UInt64UpgradeTestSchemaV1>, Tappy<tests::UInt64UpgradeTestSchemaV2>>();
  REQUIRE(python_upgrader->upgrade_required());

  const auto cpp_upgrader =
    make_cpp_upgrader<Tappy<tests::UInt64UpgradeTestSchemaV1>, Tappy<tests::UInt64UpgradeTestSchemaV2>>();
  REQUIRE(cpp_upgrader->upgrade_required());

  validate_upgradability<Tappy<tests::UInt64UpgradeTestSchemaV1>, Tappy<tests::UInt64UpgradeTestSchemaV2>>();

  Tappy<tests::UInt64UpgradeTestSchemaV1> input_instance{};
  input_instance.set_int8_field(11);
  input_instance.set_int16_field(12);
  input_instance.set_int32_field(13);
  input_instance.set_int64_field(14);
  input_instance.set_uint8_field(21);
  input_instance.set_uint16_field(22);
  input_instance.set_uint32_field(23);
  input_instance.set_uint64_field(24);
  input_instance.set_float32_field(25);
  input_instance.set_float64_field(26);
  Tappy<tests::UInt64UpgradeTestSchemaV2> python_instance{};
  Tappy<tests::UInt64UpgradeTestSchemaV2> cpp_instance{};
  std::memset(&python_instance, 0, sizeof(python_instance));
  std::memset(&cpp_instance, 0, sizeof(cpp_instance));

  python_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&python_instance, 1U}));
  cpp_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&cpp_instance, 1U}));

  const auto check_python = GENERATE(true, false);
  CAPTURE(check_python);
  const auto checked_instance = check_python ? python_instance : cpp_instance;

  REQUIRE(checked_instance.get_int8_field() == static_cast<int8_t>(input_instance.get_int8_field()));
  REQUIRE(checked_instance.get_int16_field() == static_cast<int16_t>(input_instance.get_int16_field()));
  REQUIRE(checked_instance.get_int32_field() == static_cast<int32_t>(input_instance.get_int32_field()));
  REQUIRE(checked_instance.get_int64_field() == static_cast<int64_t>(input_instance.get_int64_field()));
  REQUIRE(checked_instance.get_uint8_field() == static_cast<uint8_t>(input_instance.get_uint8_field()));
  REQUIRE(checked_instance.get_uint16_field() == static_cast<uint16_t>(input_instance.get_uint16_field()));
  REQUIRE(checked_instance.get_uint32_field() == static_cast<uint32_t>(input_instance.get_uint32_field()));
  REQUIRE(checked_instance.get_uint64_field() == input_instance.get_uint64_field());
  REQUIRE(checked_instance.get_float32_field() == static_cast<float>(input_instance.get_float32_field()));
  REQUIRE(checked_instance.get_float64_field() == static_cast<double>(input_instance.get_float64_field()));
  REQUIRE(checked_instance.get_new_field() == 42);

  REQUIRE(
    std::ranges::equal(std::as_bytes(std::span{&cpp_instance, 1U}), std::as_bytes(std::span{&python_instance, 1U})));
}

TEST_CASE("Schema upgrade")
{
  // Initialize python for the log schema upgrader
  python::python_init(python::InitializationMode::unit_test);

  const auto python_upgrader =
    make_python_upgrader<Tappy<tests::SchemaUpgradeTestSchemaV1>, Tappy<tests::SchemaUpgradeTestSchemaV2>>();
  REQUIRE(python_upgrader->upgrade_required());

  const auto cpp_upgrader =
    make_cpp_upgrader<Tappy<tests::SchemaUpgradeTestSchemaV1>, Tappy<tests::SchemaUpgradeTestSchemaV2>>();
  REQUIRE(cpp_upgrader->upgrade_required());

  validate_upgradability<Tappy<tests::SchemaUpgradeTestSchemaV1>, Tappy<tests::SchemaUpgradeTestSchemaV2>>();

  Tappy<tests::SchemaUpgradeTestSchemaV1> input_instance{};
  input_instance.get_mutable_upgraded_field().set_upgraded_field(11U);
  Tappy<tests::SchemaUpgradeTestSchemaV2> python_instance{};
  Tappy<tests::SchemaUpgradeTestSchemaV2> cpp_instance{};
  std::memset(&python_instance, 0, sizeof(python_instance));
  std::memset(&cpp_instance, 0, sizeof(cpp_instance));

  python_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&python_instance, 1U}));
  cpp_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&cpp_instance, 1U}));

  const auto check_python = GENERATE(true, false);
  CAPTURE(check_python);
  const auto checked_instance = check_python ? python_instance : cpp_instance;

  REQUIRE(
    checked_instance.get_upgraded_field().get_upgraded_field() ==
    static_cast<uint64_t>(input_instance.get_upgraded_field().get_upgraded_field()));
  REQUIRE(checked_instance.get_upgraded_field().get_added_field() == 42U);
  REQUIRE(checked_instance.get_added_field().get_upgraded_field() == 0U);
  REQUIRE(checked_instance.get_added_field().get_added_field() == 42U);

  REQUIRE(
    std::ranges::equal(std::as_bytes(std::span{&cpp_instance, 1U}), std::as_bytes(std::span{&python_instance, 1U})));
}

TEST_CASE("Float32 upgrade")
{
  // Initialize python for the log schema upgrader
  python::python_init(python::InitializationMode::unit_test);

  const auto python_upgrader =
    make_python_upgrader<Tappy<tests::Float32UpgradeTestSchemaV1>, Tappy<tests::Float32UpgradeTestSchemaV2>>();
  REQUIRE(python_upgrader->upgrade_required());

  const auto cpp_upgrader =
    make_cpp_upgrader<Tappy<tests::Float32UpgradeTestSchemaV1>, Tappy<tests::Float32UpgradeTestSchemaV2>>();
  REQUIRE(cpp_upgrader->upgrade_required());

  validate_upgradability<Tappy<tests::Float32UpgradeTestSchemaV1>, Tappy<tests::Float32UpgradeTestSchemaV2>>();

  Tappy<tests::Float32UpgradeTestSchemaV1> input_instance{};
  input_instance.set_int8_field(11.1f);
  input_instance.set_int16_field(12.2f);
  input_instance.set_int32_field(13.3f);
  input_instance.set_int64_field(14.4f);
  input_instance.set_uint8_field(21.1f);
  input_instance.set_uint16_field(22.2f);
  input_instance.set_uint32_field(23.3f);
  input_instance.set_uint64_field(24.4f);
  input_instance.set_float32_field(11.3f);
  input_instance.set_float64_field(77.7f);
  Tappy<tests::Float32UpgradeTestSchemaV2> python_instance{};
  Tappy<tests::Float32UpgradeTestSchemaV2> cpp_instance{};
  std::memset(&python_instance, 0, sizeof(python_instance));
  std::memset(&cpp_instance, 0, sizeof(cpp_instance));

  python_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&python_instance, 1U}));
  cpp_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&cpp_instance, 1U}));

  const auto check_python = GENERATE(true, false);
  CAPTURE(check_python);
  const auto checked_instance = check_python ? python_instance : cpp_instance;

  REQUIRE(checked_instance.get_int8_field() == static_cast<int8_t>(input_instance.get_int8_field()));
  REQUIRE(checked_instance.get_int16_field() == static_cast<int16_t>(input_instance.get_int16_field()));
  REQUIRE(checked_instance.get_int32_field() == static_cast<int32_t>(input_instance.get_int32_field()));
  REQUIRE(checked_instance.get_int64_field() == static_cast<int64_t>(input_instance.get_int64_field()));
  REQUIRE(checked_instance.get_uint8_field() == static_cast<uint8_t>(input_instance.get_uint8_field()));
  REQUIRE(checked_instance.get_uint16_field() == static_cast<uint16_t>(input_instance.get_uint16_field()));
  REQUIRE(checked_instance.get_uint32_field() == static_cast<uint32_t>(input_instance.get_uint32_field()));
  REQUIRE(checked_instance.get_uint64_field() == static_cast<uint64_t>(input_instance.get_uint64_field()));
  REQUIRE(checked_instance.get_float32_field() == input_instance.get_float32_field());
  REQUIRE(checked_instance.get_float64_field() == static_cast<double>(input_instance.get_float64_field()));
  REQUIRE(checked_instance.get_new_field() == 51.2f);

  REQUIRE(
    std::ranges::equal(std::as_bytes(std::span{&cpp_instance, 1U}), std::as_bytes(std::span{&python_instance, 1U})));
}

TEST_CASE("Float64 upgrade")
{
  // Initialize python for the log schema upgrader
  python::python_init(python::InitializationMode::unit_test);

  const auto python_upgrader =
    make_python_upgrader<Tappy<tests::Float64UpgradeTestSchemaV1>, Tappy<tests::Float64UpgradeTestSchemaV2>>();
  REQUIRE(python_upgrader->upgrade_required());

  const auto cpp_upgrader =
    make_cpp_upgrader<Tappy<tests::Float64UpgradeTestSchemaV1>, Tappy<tests::Float64UpgradeTestSchemaV2>>();
  REQUIRE(cpp_upgrader->upgrade_required());

  validate_upgradability<Tappy<tests::Float64UpgradeTestSchemaV1>, Tappy<tests::Float64UpgradeTestSchemaV2>>();

  Tappy<tests::Float64UpgradeTestSchemaV1> input_instance{};
  input_instance.set_int8_field(11.1);
  input_instance.set_int16_field(12.2);
  input_instance.set_int32_field(13.3);
  input_instance.set_int64_field(14.4);
  input_instance.set_uint8_field(21.1);
  input_instance.set_uint16_field(22.2);
  input_instance.set_uint32_field(23.3);
  input_instance.set_uint64_field(24.4);
  input_instance.set_float32_field(11.3);
  input_instance.set_float64_field(77.7);
  Tappy<tests::Float64UpgradeTestSchemaV2> python_instance{};
  Tappy<tests::Float64UpgradeTestSchemaV2> cpp_instance{};
  std::memset(&python_instance, 0, sizeof(python_instance));
  std::memset(&cpp_instance, 0, sizeof(cpp_instance));

  python_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&python_instance, 1U}));
  cpp_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&cpp_instance, 1U}));

  const auto check_python = GENERATE(true, false);
  CAPTURE(check_python);
  const auto checked_instance = check_python ? python_instance : cpp_instance;

  REQUIRE(checked_instance.get_int8_field() == static_cast<int8_t>(input_instance.get_int8_field()));
  REQUIRE(checked_instance.get_int16_field() == static_cast<int16_t>(input_instance.get_int16_field()));
  REQUIRE(checked_instance.get_int32_field() == static_cast<int32_t>(input_instance.get_int32_field()));
  REQUIRE(checked_instance.get_int64_field() == static_cast<int64_t>(input_instance.get_int64_field()));
  REQUIRE(checked_instance.get_uint8_field() == static_cast<uint8_t>(input_instance.get_uint8_field()));
  REQUIRE(checked_instance.get_uint16_field() == static_cast<uint16_t>(input_instance.get_uint16_field()));
  REQUIRE(checked_instance.get_uint32_field() == static_cast<uint32_t>(input_instance.get_uint32_field()));
  REQUIRE(checked_instance.get_uint64_field() == static_cast<uint64_t>(input_instance.get_uint64_field()));
  REQUIRE(checked_instance.get_float32_field() == static_cast<float>(input_instance.get_float32_field()));
  REQUIRE(checked_instance.get_float64_field() == input_instance.get_float64_field());
  REQUIRE(checked_instance.get_new_field() == 51.2);

  REQUIRE(
    std::ranges::equal(std::as_bytes(std::span{&cpp_instance, 1U}), std::as_bytes(std::span{&python_instance, 1U})));
}

TEST_CASE("Primitive types upgrade")
{
  // Initialize python for the log schema upgrader
  python::python_init(python::InitializationMode::unit_test);

  const auto python_upgrader =
    make_python_upgrader<Tappy<tests::PrimitiveUpgradeTestSchemaV1>, Tappy<tests::PrimitiveUpgradeTestSchemaV2>>();
  REQUIRE(python_upgrader->upgrade_required());

  const auto cpp_upgrader =
    make_cpp_upgrader<Tappy<tests::PrimitiveUpgradeTestSchemaV1>, Tappy<tests::PrimitiveUpgradeTestSchemaV2>>();
  REQUIRE(cpp_upgrader->upgrade_required());

  validate_upgradability<Tappy<tests::PrimitiveUpgradeTestSchemaV1>, Tappy<tests::PrimitiveUpgradeTestSchemaV2>>();

  Tappy<tests::PrimitiveUpgradeTestSchemaV1> input_instance{};
  input_instance.set_byte_field(std::byte{42});
  input_instance.set_uuid_field(SchemaUuid::random_uuid());
  input_instance.set_bool_field(true);
  Tappy<tests::PrimitiveUpgradeTestSchemaV2> python_instance{};
  Tappy<tests::PrimitiveUpgradeTestSchemaV2> cpp_instance{};
  std::memset(&python_instance, 0, sizeof(python_instance));
  std::memset(&cpp_instance, 0, sizeof(cpp_instance));

  python_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&python_instance, 1U}));
  cpp_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&cpp_instance, 1U}));

  const auto check_python = GENERATE(true, false);
  CAPTURE(check_python);
  const auto checked_instance = check_python ? python_instance : cpp_instance;

  REQUIRE(checked_instance.get_byte_field() == input_instance.get_byte_field());
  REQUIRE(checked_instance.get_uuid_field() == input_instance.get_uuid_field());
  REQUIRE(checked_instance.get_bool_field() == input_instance.get_bool_field());
  REQUIRE(checked_instance.get_added_byte_field() == std::byte{0});
  REQUIRE(checked_instance.get_added_uuid_field().is_nil());
  REQUIRE(checked_instance.get_added_bool_field());

  REQUIRE(
    std::ranges::equal(std::as_bytes(std::span{&cpp_instance, 1U}), std::as_bytes(std::span{&python_instance, 1U})));
}

TEST_CASE("SyncTime upgrade")
{
  // Initialize python for the log schema upgrader
  python::python_init(python::InitializationMode::unit_test);

  const auto python_upgrader =
    make_python_upgrader<Tappy<tests::SyncTimeUpgradeTestSchemaV1>, Tappy<tests::SyncTimeUpgradeTestSchemaV2>>();
  REQUIRE(python_upgrader->upgrade_required());

  const auto cpp_upgrader =
    make_cpp_upgrader<Tappy<tests::SyncTimeUpgradeTestSchemaV1>, Tappy<tests::SyncTimeUpgradeTestSchemaV2>>();
  REQUIRE(cpp_upgrader->upgrade_required());

  validate_upgradability<Tappy<tests::SyncTimeUpgradeTestSchemaV1>, Tappy<tests::SyncTimeUpgradeTestSchemaV2>>();

  Tappy<tests::SyncTimeUpgradeTestSchemaV1> input_instance{};
  input_instance.set_int8_field(11);
  input_instance.set_int16_field(12);
  input_instance.set_int32_field(13);
  input_instance.set_int64_field(14);
  input_instance.set_uint8_field(21);
  input_instance.set_uint16_field(22);
  input_instance.set_uint32_field(23);
  input_instance.set_uint64_field(24);
  input_instance.set_synctime_field(jewels::time::SyncTime{std::chrono::nanoseconds(31)});
  Tappy<tests::SyncTimeUpgradeTestSchemaV2> python_instance{};
  Tappy<tests::SyncTimeUpgradeTestSchemaV2> cpp_instance{};
  std::memset(&python_instance, 0, sizeof(python_instance));
  std::memset(&cpp_instance, 0, sizeof(cpp_instance));

  python_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&python_instance, 1U}));
  cpp_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&cpp_instance, 1U}));

  const auto check_python = GENERATE(true, false);
  CAPTURE(check_python);
  const auto checked_instance = check_python ? python_instance : cpp_instance;

  REQUIRE(checked_instance.get_int8_field().time_since_epoch() == std::chrono::nanoseconds(11));
  REQUIRE(checked_instance.get_int16_field().time_since_epoch() == std::chrono::nanoseconds(12));
  REQUIRE(checked_instance.get_int32_field().time_since_epoch() == std::chrono::nanoseconds(13));
  REQUIRE(checked_instance.get_int64_field().time_since_epoch() == std::chrono::nanoseconds(14));
  REQUIRE(checked_instance.get_uint8_field().time_since_epoch() == std::chrono::nanoseconds(21));
  REQUIRE(checked_instance.get_uint16_field().time_since_epoch() == std::chrono::nanoseconds(22));
  REQUIRE(checked_instance.get_uint32_field().time_since_epoch() == std::chrono::nanoseconds(23));
  REQUIRE(checked_instance.get_uint64_field().time_since_epoch() == std::chrono::nanoseconds(24));
  REQUIRE(checked_instance.get_synctime_field() == input_instance.get_synctime_field());

  REQUIRE(
    std::ranges::equal(std::as_bytes(std::span{&cpp_instance, 1U}), std::as_bytes(std::span{&python_instance, 1U})));
}

TEST_CASE("Duration upgrade")
{
  // Initialize python for the log schema upgrader
  python::python_init(python::InitializationMode::unit_test);

  const auto python_upgrader =
    make_python_upgrader<Tappy<tests::DurationUpgradeTestSchemaV1>, Tappy<tests::DurationUpgradeTestSchemaV2>>();
  REQUIRE(python_upgrader->upgrade_required());

  const auto cpp_upgrader =
    make_cpp_upgrader<Tappy<tests::DurationUpgradeTestSchemaV1>, Tappy<tests::DurationUpgradeTestSchemaV2>>();
  REQUIRE(cpp_upgrader->upgrade_required());

  validate_upgradability<Tappy<tests::DurationUpgradeTestSchemaV1>, Tappy<tests::DurationUpgradeTestSchemaV2>>();

  Tappy<tests::DurationUpgradeTestSchemaV1> input_instance{};
  input_instance.set_int8_field(11);
  input_instance.set_int16_field(12);
  input_instance.set_int32_field(13);
  input_instance.set_int64_field(14);
  input_instance.set_uint8_field(21);
  input_instance.set_uint16_field(22);
  input_instance.set_uint32_field(23);
  input_instance.set_uint64_field(24);
  input_instance.set_duration_field(std::chrono::nanoseconds(31));
  Tappy<tests::DurationUpgradeTestSchemaV2> python_instance{};
  Tappy<tests::DurationUpgradeTestSchemaV2> cpp_instance{};
  std::memset(&python_instance, 0, sizeof(python_instance));
  std::memset(&cpp_instance, 0, sizeof(cpp_instance));

  python_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&python_instance, 1U}));
  cpp_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&cpp_instance, 1U}));

  const auto check_python = GENERATE(true, false);
  CAPTURE(check_python);
  const auto checked_instance = check_python ? python_instance : cpp_instance;

  REQUIRE(checked_instance.get_int8_field() == std::chrono::nanoseconds(11));
  REQUIRE(checked_instance.get_int16_field() == std::chrono::nanoseconds(12));
  REQUIRE(checked_instance.get_int32_field() == std::chrono::nanoseconds(13));
  REQUIRE(checked_instance.get_int64_field() == std::chrono::nanoseconds(14));
  REQUIRE(checked_instance.get_uint8_field() == std::chrono::nanoseconds(21));
  REQUIRE(checked_instance.get_uint16_field() == std::chrono::nanoseconds(22));
  REQUIRE(checked_instance.get_uint32_field() == std::chrono::nanoseconds(23));
  REQUIRE(checked_instance.get_uint64_field() == std::chrono::nanoseconds(24));
  REQUIRE(checked_instance.get_duration_field() == input_instance.get_duration_field());

  REQUIRE(
    std::ranges::equal(std::as_bytes(std::span{&cpp_instance, 1U}), std::as_bytes(std::span{&python_instance, 1U})));
}

TEST_CASE("Strong type upgrade")
{
  // Initialize python for the log schema upgrader
  python::python_init(python::InitializationMode::unit_test);

  const auto python_upgrader =
    make_python_upgrader<Tappy<tests::StrongTypeUpgradeTestSchemaV1>, Tappy<tests::StrongTypeUpgradeTestSchemaV2>>();
  REQUIRE(python_upgrader->upgrade_required());

  const auto cpp_upgrader =
    make_cpp_upgrader<Tappy<tests::StrongTypeUpgradeTestSchemaV1>, Tappy<tests::StrongTypeUpgradeTestSchemaV2>>();
  REQUIRE(cpp_upgrader->upgrade_required());

  validate_upgradability<Tappy<tests::StrongTypeUpgradeTestSchemaV1>, Tappy<tests::StrongTypeUpgradeTestSchemaV2>>();

  Tappy<tests::StrongTypeUpgradeTestSchemaV1> input_instance{};
  input_instance.set_int32_field(11);
  input_instance.set_strong_int32_field(tests::make_strong_int32(12));
  input_instance.set_int64_field(13);
  input_instance.set_strong_int64_field(tests::make_strong_int64(14));
  input_instance.set_float32_field(15.15f);
  input_instance.set_strong_float32_field(tests::make_strong_float32(16.16f));
  input_instance.set_float64_field(17.17);
  input_instance.set_strong_float64_field(tests::make_strong_float64(18.18));
  input_instance.set_byte_field(std::byte{19});
  input_instance.set_strong_byte_field(tests::make_strong_byte(std::byte{20}));
  Tappy<tests::StrongTypeUpgradeTestSchemaV2> python_instance{};
  Tappy<tests::StrongTypeUpgradeTestSchemaV2> cpp_instance{};
  std::memset(&python_instance, 0, sizeof(python_instance));
  std::memset(&cpp_instance, 0, sizeof(cpp_instance));

  python_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&python_instance, 1U}));
  cpp_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&cpp_instance, 1U}));

  const auto check_python = GENERATE(true, false);
  CAPTURE(check_python);
  const auto checked_instance = check_python ? python_instance : cpp_instance;

  REQUIRE(checked_instance.get_int32_field().get() == static_cast<int64_t>(input_instance.get_int32_field()));
  REQUIRE(
    checked_instance.get_strong_int32_field() == static_cast<int64_t>(input_instance.get_strong_int32_field().get()));
  REQUIRE(checked_instance.get_int64_field().get() == static_cast<int32_t>(input_instance.get_int64_field()));
  REQUIRE(
    checked_instance.get_strong_int64_field() == static_cast<int32_t>(input_instance.get_strong_int64_field().get()));
  REQUIRE(checked_instance.get_float32_field().get() == static_cast<double>(input_instance.get_float32_field()));
  REQUIRE(
    checked_instance.get_strong_float32_field() ==
    static_cast<double>(input_instance.get_strong_float32_field().get()));
  REQUIRE(checked_instance.get_float64_field().get() == static_cast<float>(input_instance.get_float64_field()));
  REQUIRE(
    checked_instance.get_strong_float64_field() == static_cast<float>(input_instance.get_strong_float64_field().get()));
  REQUIRE(checked_instance.get_byte_field().get() == input_instance.get_byte_field());
  REQUIRE(checked_instance.get_strong_byte_field() == input_instance.get_strong_byte_field().get());
  REQUIRE(checked_instance.get_added_strong_int32_field().get() == 21);
  REQUIRE(checked_instance.get_added_strong_int64_field().get() == 22);
  REQUIRE(checked_instance.get_added_strong_float32_field().get() == 23.23f);
  REQUIRE(checked_instance.get_added_strong_float64_field().get() == 24.24);
  REQUIRE(checked_instance.get_added_strong_byte_field().get() == std::byte{0});

  REQUIRE(
    std::ranges::equal(std::as_bytes(std::span{&cpp_instance, 1U}), std::as_bytes(std::span{&python_instance, 1U})));
}

TEST_CASE("Fixed array upgrade")
{
  // Initialize python for the log schema upgrader
  python::python_init(python::InitializationMode::unit_test);

  const auto python_upgrader =
    make_python_upgrader<Tappy<tests::FixedArrayUpgradeTestSchemaV1>, Tappy<tests::FixedArrayUpgradeTestSchemaV2>>();
  REQUIRE(python_upgrader->upgrade_required());

  const auto cpp_upgrader =
    make_cpp_upgrader<Tappy<tests::FixedArrayUpgradeTestSchemaV1>, Tappy<tests::FixedArrayUpgradeTestSchemaV2>>();
  REQUIRE(cpp_upgrader->upgrade_required());

  validate_upgradability<Tappy<tests::FixedArrayUpgradeTestSchemaV1>, Tappy<tests::FixedArrayUpgradeTestSchemaV2>>();

  Tappy<tests::FixedArrayUpgradeTestSchemaV1> input_instance{};
  input_instance.set_int32_array_field(std::array<int64_t, 2U>{11, 12});
  input_instance.set_float64_array_field(std::array<float, 2U>{21.21f, 22.22f});
  input_instance.set_uuid_array_field(std::array<SchemaUuid, 2U>{SchemaUuid::random_uuid(), SchemaUuid::random_uuid()});
  input_instance.set_uint64_2d_array_field(std::array<std::array<uint32_t, 2U>, 2U>{{{{31U, 32U}}, {{33U, 34U}}}});
  Tappy<tests::UpgradedMemberSchemaTypeV1> schema1;
  schema1.set_upgraded_field(41U);
  Tappy<tests::UpgradedMemberSchemaTypeV1> schema2;
  schema2.set_upgraded_field(42U);
  Tappy<tests::UpgradedMemberSchemaTypeV1> schema3;
  schema3.set_upgraded_field(43U);
  Tappy<tests::UpgradedMemberSchemaTypeV1> schema4;
  schema4.set_upgraded_field(44U);
  input_instance.set_schema_2d_array_field(
    std::array<std::array<Tappy<tests::UpgradedMemberSchemaTypeV1>, 2U>, 2U>{
      {{{schema1, schema2}}, {{schema3, schema4}}}});
  Tappy<tests::CopyableMemberSchemaTypeV1> schema5;
  schema5.set_uuid_field(SchemaUuid::random_uuid());
  schema5.set_int32_array_field(std::array<int32_t, 4U>{51, 52, 53, 54});
  Tappy<tests::CopyableMemberSchemaTypeV1> schema6;
  schema6.set_uuid_field(SchemaUuid::random_uuid());
  schema6.set_int32_array_field(std::array<int32_t, 4U>{61, 62, 63, 64});
  Tappy<tests::CopyableMemberSchemaTypeV1> schema7;
  schema7.set_uuid_field(SchemaUuid::random_uuid());
  schema7.set_int32_array_field(std::array<int32_t, 4U>{71, 72, 73, 74});
  Tappy<tests::CopyableMemberSchemaTypeV1> schema8;
  schema8.set_uuid_field(SchemaUuid::random_uuid());
  schema8.set_int32_array_field(std::array<int32_t, 4U>{81, 82, 83, 84});
  input_instance.set_schema_array_field(
    std::array<Tappy<tests::CopyableMemberSchemaTypeV1>, 4U>{schema5, schema6, schema7, schema8});
  input_instance.set_upgraded_fixed_array_field(std::array<uint64_t, 2U>{91, 92});
  REQUIRE(input_instance.get_underlying_upgraded_vararray_field().try_set(
    std::array<Tappy<tests::CopyableMemberSchemaTypeV1>, 2U>{schema5, schema6}));
  REQUIRE(input_instance.get_underlying_upgraded_varstring_field().try_set("0123456"));
  const auto uuid1 = SchemaUuid::random_uuid();
  input_instance.set_upgraded_optional_field(uuid1);
  input_instance.set_upgraded_instance_field(schema1);
  Tappy<tests::FixedArrayUpgradeTestSchemaV2> python_instance{};
  Tappy<tests::FixedArrayUpgradeTestSchemaV2> cpp_instance{};
  std::memset(&python_instance, 0, sizeof(python_instance));
  std::memset(&cpp_instance, 0, sizeof(cpp_instance));

  python_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&python_instance, 1U}));
  cpp_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&cpp_instance, 1U}));

  const auto check_python = GENERATE(true, false);
  CAPTURE(check_python);
  const auto checked_instance = check_python ? python_instance : cpp_instance;

  REQUIRE(std::ranges::equal(checked_instance.get_int32_array_field(), std::array<int32_t, 2U>{11, 12}));
  REQUIRE(
    std::ranges::equal(
      checked_instance.get_float64_array_field(),
      std::array<double, 2U>{static_cast<double>(21.21f), static_cast<double>(22.22f)}));
  REQUIRE(std::ranges::equal(checked_instance.get_uuid_array_field(), input_instance.get_uuid_array_field()));
  REQUIRE(
    std::ranges::equal(
      checked_instance.get_uint64_2d_array_field(),
      std::array<std::array<uint64_t, 2U>, 2U>{{{{31U, 32U}}, {{33U, 34U}}}}));
  Tappy<tests::UpgradedMemberSchemaTypeV2> expected_schema1;
  expected_schema1.set_upgraded_field(41U);
  Tappy<tests::UpgradedMemberSchemaTypeV2> expected_schema2;
  expected_schema2.set_upgraded_field(42U);
  Tappy<tests::UpgradedMemberSchemaTypeV2> expected_schema3;
  expected_schema3.set_upgraded_field(43U);
  Tappy<tests::UpgradedMemberSchemaTypeV2> expected_schema4;
  expected_schema4.set_upgraded_field(44U);
  REQUIRE(
    std::ranges::equal(
      checked_instance.get_schema_2d_array_field(),
      std::array<std::array<Tappy<tests::UpgradedMemberSchemaTypeV2>, 2U>, 2U>{
        {{{expected_schema1, expected_schema2}}, {{expected_schema3, expected_schema4}}}}));
  REQUIRE(
    std::ranges::equal(
      checked_instance.get_added_schema_2d_array_field(),
      std::array<std::array<Tappy<tests::UpgradedMemberSchemaTypeV2>, 2U>, 2U>{
        {{{Tappy<tests::UpgradedMemberSchemaTypeV2>{}, Tappy<tests::UpgradedMemberSchemaTypeV2>{}}},
         {{Tappy<tests::UpgradedMemberSchemaTypeV2>{}, Tappy<tests::UpgradedMemberSchemaTypeV2>{}}}}}));
  Tappy<tests::CopyableMemberSchemaTypeV2> expected_schema5;
  expected_schema5.set_uuid_field(schema5.get_uuid_field());
  expected_schema5.set_int32_array_field(schema5.get_int32_array_field());
  Tappy<tests::CopyableMemberSchemaTypeV2> expected_schema6;
  expected_schema6.set_uuid_field(schema6.get_uuid_field());
  expected_schema6.set_int32_array_field(schema6.get_int32_array_field());
  Tappy<tests::CopyableMemberSchemaTypeV2> expected_schema7;
  expected_schema7.set_uuid_field(schema7.get_uuid_field());
  expected_schema7.set_int32_array_field(schema7.get_int32_array_field());
  Tappy<tests::CopyableMemberSchemaTypeV2> expected_schema8;
  expected_schema8.set_uuid_field(schema8.get_uuid_field());
  expected_schema8.set_int32_array_field(schema8.get_int32_array_field());
  REQUIRE(
    std::ranges::equal(
      checked_instance.get_schema_array_field(),
      std::array<Tappy<tests::CopyableMemberSchemaTypeV2>, 4U>{
        expected_schema5, expected_schema6, expected_schema7, expected_schema8}));
  REQUIRE(
    std::ranges::equal(
      checked_instance.get_upgraded_instance_field(),
      std::array<Tappy<tests::UpgradedMemberSchemaTypeV2>, 1U>{expected_schema1}));
  REQUIRE(
    std::ranges::equal(
      checked_instance.get_upgraded_fixed_array_field(), std::array<std::array<int32_t, 2U>, 1U>{{{{91, 92}}}}));
  REQUIRE(
    std::ranges::equal(checked_instance.get_upgraded_vararray_field(), std::array{expected_schema5, expected_schema6}));
  REQUIRE(
    std::ranges::equal(checked_instance.get_upgraded_varstring_field(), std::array{'0', '1', '2', '3', '4', '5', '6'}));
  REQUIRE(std::ranges::equal(checked_instance.get_upgraded_optional_field(), std::array{uuid1}));

  REQUIRE(
    std::ranges::equal(std::as_bytes(std::span{&cpp_instance, 1U}), std::as_bytes(std::span{&python_instance, 1U})));
}

TEST_CASE("Variable array upgrade")
{
  // Initialize python for the log schema upgrader
  python::python_init(python::InitializationMode::unit_test);

  const auto python_upgrader =
    make_python_upgrader<Tappy<tests::VarArrayUpgradeTestSchemaV1>, Tappy<tests::VarArrayUpgradeTestSchemaV2>>();
  REQUIRE(python_upgrader->upgrade_required());

  const auto cpp_upgrader =
    make_cpp_upgrader<Tappy<tests::VarArrayUpgradeTestSchemaV1>, Tappy<tests::VarArrayUpgradeTestSchemaV2>>();
  REQUIRE(cpp_upgrader->upgrade_required());

  validate_upgradability<Tappy<tests::VarArrayUpgradeTestSchemaV1>, Tappy<tests::VarArrayUpgradeTestSchemaV2>>();

  Tappy<tests::VarArrayUpgradeTestSchemaV1> input_instance{};
  REQUIRE(input_instance.get_underlying_int32_array_field().try_set(std::array<int64_t, 2U>{11, 12}));
  REQUIRE(input_instance.get_underlying_float64_array_field().try_set(std::array<float, 2U>{21.21f, 22.22f}));
  REQUIRE(input_instance.get_underlying_uuid_array_field().try_set(
    std::array<SchemaUuid, 2U>{SchemaUuid::random_uuid(), SchemaUuid::random_uuid()}));
  input_instance.get_underlying_uint64_2d_array_field().emplace_back(31U, 32U);
  input_instance.get_underlying_uint64_2d_array_field().emplace_back(33U, 34U);
  Tappy<tests::UpgradedMemberSchemaTypeV1> schema1;
  schema1.set_upgraded_field(41U);
  Tappy<tests::UpgradedMemberSchemaTypeV1> schema2;
  schema2.set_upgraded_field(42U);
  Tappy<tests::UpgradedMemberSchemaTypeV1> schema3;
  schema3.set_upgraded_field(43U);
  Tappy<tests::UpgradedMemberSchemaTypeV1> schema4;
  schema4.set_upgraded_field(44U);
  input_instance.get_underlying_schema_2d_array_field().emplace_back(schema1, schema2);
  input_instance.get_underlying_schema_2d_array_field().emplace_back(schema3, schema4);
  Tappy<tests::CopyableMemberSchemaTypeV1> schema5;
  schema5.set_uuid_field(SchemaUuid::random_uuid());
  schema5.set_int32_array_field(std::array<int32_t, 4U>{51, 52, 53, 54});
  Tappy<tests::CopyableMemberSchemaTypeV1> schema6;
  schema6.set_uuid_field(SchemaUuid::random_uuid());
  schema6.set_int32_array_field(std::array<int32_t, 4U>{61, 62, 63, 64});
  Tappy<tests::CopyableMemberSchemaTypeV1> schema7;
  schema7.set_uuid_field(SchemaUuid::random_uuid());
  schema7.set_int32_array_field(std::array<int32_t, 4U>{71, 72, 73, 74});
  Tappy<tests::CopyableMemberSchemaTypeV1> schema8;
  schema8.set_uuid_field(SchemaUuid::random_uuid());
  schema8.set_int32_array_field(std::array<int32_t, 4U>{81, 82, 83, 84});
  REQUIRE(input_instance.get_underlying_schema_array_field().try_set(
    std::array<Tappy<tests::CopyableMemberSchemaTypeV1>, 4U>{schema5, schema6, schema7, schema8}));
  input_instance.set_upgraded_fixed_array_field(
    std::array<Tappy<tests::CopyableMemberSchemaTypeV1>, 2U>{schema5, schema6});
  REQUIRE(input_instance.get_underlying_upgraded_vararray_field().try_set(std::array<uint64_t, 2U>{91U, 92U}));
  REQUIRE(input_instance.get_underlying_upgraded_varstring_field().try_set("0123456"));
  const auto uuid1 = SchemaUuid::random_uuid();
  input_instance.set_upgraded_optional_field(uuid1);
  input_instance.set_upgraded_instance_field(schema1);
  Tappy<tests::VarArrayUpgradeTestSchemaV2> python_instance{};
  Tappy<tests::VarArrayUpgradeTestSchemaV2> cpp_instance{};
  std::memset(&python_instance, 0, sizeof(python_instance));
  std::memset(&cpp_instance, 0, sizeof(cpp_instance));

  python_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&python_instance, 1U}));
  cpp_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&cpp_instance, 1U}));

  const auto check_python = GENERATE(true, false);
  CAPTURE(check_python);
  const auto checked_instance = check_python ? python_instance : cpp_instance;

  REQUIRE(std::ranges::equal(checked_instance.get_int32_array_field(), std::array<int32_t, 2U>{11, 12}));
  REQUIRE(
    std::ranges::equal(
      checked_instance.get_float64_array_field(),
      std::array<double, 2U>{static_cast<double>(21.21f), static_cast<double>(22.22f)}));
  REQUIRE(std::ranges::equal(checked_instance.get_uuid_array_field(), input_instance.get_uuid_array_field()));
  REQUIRE(
    std::ranges::equal(
      checked_instance.get_uint64_2d_array_field() | std::views::join, std::array<uint64_t, 4U>{31U, 32U, 33U, 34U}));
  Tappy<tests::UpgradedMemberSchemaTypeV2> expected_schema1;
  expected_schema1.set_upgraded_field(41U);
  Tappy<tests::UpgradedMemberSchemaTypeV2> expected_schema2;
  expected_schema2.set_upgraded_field(42U);
  Tappy<tests::UpgradedMemberSchemaTypeV2> expected_schema3;
  expected_schema3.set_upgraded_field(43U);
  Tappy<tests::UpgradedMemberSchemaTypeV2> expected_schema4;
  expected_schema4.set_upgraded_field(44U);
  REQUIRE(
    std::ranges::equal(
      checked_instance.get_schema_2d_array_field() | std::views::join,
      std::array<Tappy<tests::UpgradedMemberSchemaTypeV2>, 4U>{
        expected_schema1, expected_schema2, expected_schema3, expected_schema4}));
  REQUIRE(checked_instance.get_added_schema_2d_array_field().empty());
  Tappy<tests::CopyableMemberSchemaTypeV2> expected_schema5;
  expected_schema5.set_uuid_field(schema5.get_uuid_field());
  expected_schema5.set_int32_array_field(schema5.get_int32_array_field());
  Tappy<tests::CopyableMemberSchemaTypeV2> expected_schema6;
  expected_schema6.set_uuid_field(schema6.get_uuid_field());
  expected_schema6.set_int32_array_field(schema6.get_int32_array_field());
  Tappy<tests::CopyableMemberSchemaTypeV2> expected_schema7;
  expected_schema7.set_uuid_field(schema7.get_uuid_field());
  expected_schema7.set_int32_array_field(schema7.get_int32_array_field());
  Tappy<tests::CopyableMemberSchemaTypeV2> expected_schema8;
  expected_schema8.set_uuid_field(schema8.get_uuid_field());
  expected_schema8.set_int32_array_field(schema8.get_int32_array_field());
  REQUIRE(
    std::ranges::equal(
      checked_instance.get_schema_array_field(),
      std::array<Tappy<tests::CopyableMemberSchemaTypeV2>, 4U>{
        expected_schema5, expected_schema6, expected_schema7, expected_schema8}));
  REQUIRE(
    std::ranges::equal(
      checked_instance.get_upgraded_instance_field(),
      std::array<Tappy<tests::UpgradedMemberSchemaTypeV2>, 1U>{expected_schema1}));
  REQUIRE(
    std::ranges::equal(
      checked_instance.get_upgraded_fixed_array_field(), std::array{expected_schema5, expected_schema6}));
  REQUIRE(
    std::ranges::equal(
      checked_instance.get_upgraded_vararray_field() | std::views::join, std::array<int32_t, 2U>{91, 92}));
  REQUIRE(
    std::ranges::equal(checked_instance.get_upgraded_varstring_field(), std::array{'0', '1', '2', '3', '4', '5', '6'}));
  REQUIRE(std::ranges::equal(checked_instance.get_upgraded_optional_field(), std::array{uuid1}));

  REQUIRE(
    std::ranges::equal(std::as_bytes(std::span{&cpp_instance, 1U}), std::as_bytes(std::span{&python_instance, 1U})));
}

TEST_CASE("Optional upgrade")
{
  // Initialize python for the log schema upgrader
  python::python_init(python::InitializationMode::unit_test);

  const auto python_upgrader =
    make_python_upgrader<Tappy<tests::OptionalUpgradeTestSchemaV1>, Tappy<tests::OptionalUpgradeTestSchemaV2>>();
  REQUIRE(python_upgrader->upgrade_required());

  const auto cpp_upgrader =
    make_cpp_upgrader<Tappy<tests::OptionalUpgradeTestSchemaV1>, Tappy<tests::OptionalUpgradeTestSchemaV2>>();
  REQUIRE(cpp_upgrader->upgrade_required());

  validate_upgradability<Tappy<tests::OptionalUpgradeTestSchemaV1>, Tappy<tests::OptionalUpgradeTestSchemaV2>>();

  Tappy<tests::OptionalUpgradeTestSchemaV1> input_instance{};
  input_instance.set_int32_optional_field(11);
  input_instance.set_float64_optional_field(12.12f);
  input_instance.set_uuid_optional_field(SchemaUuid::random_uuid());
  Tappy<tests::UpgradedMemberSchemaTypeV1> schema1;
  schema1.set_upgraded_field(41U);
  input_instance.set_schema_optional_field1(schema1);
  Tappy<tests::CopyableMemberSchemaTypeV1> schema2;
  schema2.set_uuid_field(SchemaUuid::random_uuid());
  schema2.set_int32_array_field(std::array<int32_t, 4U>{51, 52, 53, 54});
  input_instance.set_schema_optional_field2(schema2);
  REQUIRE(input_instance.get_underlying_upgraded_instance_field().try_set("0123456"));
  input_instance.set_upgraded_fixed_array_field(std::array{schema2});
  REQUIRE(input_instance.get_underlying_upgraded_vararray_field().try_set(std::array<uint64_t, 1U>{61U}));
  REQUIRE(input_instance.get_underlying_upgraded_varstring_field().try_set("X"));
  Tappy<tests::OptionalUpgradeTestSchemaV2> python_instance{};
  Tappy<tests::OptionalUpgradeTestSchemaV2> cpp_instance{};
  std::memset(&python_instance, 0, sizeof(python_instance));
  std::memset(&cpp_instance, 0, sizeof(cpp_instance));

  python_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&python_instance, 1U}));
  cpp_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&cpp_instance, 1U}));

  const auto check_python = GENERATE(true, false);
  CAPTURE(check_python);
  const auto checked_instance = check_python ? python_instance : cpp_instance;

  REQUIRE(checked_instance.value_int32_optional_field() == 11);
  REQUIRE(checked_instance.value_float64_optional_field() == static_cast<double>(12.12f));
  Tappy<tests::UpgradedMemberSchemaTypeV2> expected_schema1;
  expected_schema1.set_upgraded_field(41U);
  REQUIRE(checked_instance.value_schema_optional_field1() == expected_schema1);
  Tappy<tests::CopyableMemberSchemaTypeV2> expected_schema2;
  expected_schema2.set_uuid_field(schema2.get_uuid_field());
  expected_schema2.set_int32_array_field(schema2.get_int32_array_field());
  REQUIRE(checked_instance.value_schema_optional_field2() == expected_schema2);
  REQUIRE_FALSE(checked_instance.has_added_schema_optional_field());
  REQUIRE(checked_instance.value_upgraded_instance_field() == std::string_view{"0123456"});
  REQUIRE(checked_instance.value_upgraded_fixed_array_field() == expected_schema2);
  REQUIRE(checked_instance.value_upgraded_vararray_field() == 61U);
  REQUIRE(checked_instance.value_upgraded_varstring_field() == 'X');

  REQUIRE(
    std::ranges::equal(std::as_bytes(std::span{&cpp_instance, 1U}), std::as_bytes(std::span{&python_instance, 1U})));
}

TEST_CASE("VarString upgrade")
{
  // Initialize python for the log schema upgrader
  python::python_init(python::InitializationMode::unit_test);

  const auto python_upgrader =
    make_python_upgrader<Tappy<tests::VarStringUpgradeTestSchemaV1>, Tappy<tests::VarStringUpgradeTestSchemaV2>>();
  REQUIRE(python_upgrader->upgrade_required());

  const auto cpp_upgrader =
    make_cpp_upgrader<Tappy<tests::VarStringUpgradeTestSchemaV1>, Tappy<tests::VarStringUpgradeTestSchemaV2>>();
  REQUIRE(cpp_upgrader->upgrade_required());

  validate_upgradability<Tappy<tests::VarStringUpgradeTestSchemaV1>, Tappy<tests::VarStringUpgradeTestSchemaV2>>();

  Tappy<tests::VarStringUpgradeTestSchemaV1> input_instance{};
  REQUIRE(input_instance.get_underlying_varstring_17_field().try_set("0123456789012345"));
  REQUIRE(input_instance.get_underlying_varstring_27_field().try_set("abcdefghijklmnop"));
  REQUIRE(input_instance.get_underlying_varstring_32_field().try_set("9876543210987654321098765432109"));
  REQUIRE(input_instance.get_underlying_varstring_64_field().try_set("ABCDEFGHIJKLMNOPQRSTUVWXYZABCDE"));
  REQUIRE(input_instance.get_underlying_varstring_16_field().try_set("zyxwvutsrqponml"));
  REQUIRE(input_instance.get_underlying_upgraded_vararray_field().try_set(std::array<int8_t, 3U>{'0', '1', '2'}));
  input_instance.set_upgraded_fixed_array_field(std::array<int8_t, 3U>{'a', 'b', 'c'});
  input_instance.set_upgraded_optional_field('X');
  input_instance.set_upgraded_int8_field('A');
  Tappy<tests::VarStringUpgradeTestSchemaV2> python_instance{};
  Tappy<tests::VarStringUpgradeTestSchemaV2> cpp_instance{};
  std::memset(&python_instance, 0, sizeof(python_instance));
  std::memset(&cpp_instance, 0, sizeof(cpp_instance));

  python_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&python_instance, 1U}));
  cpp_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&cpp_instance, 1U}));

  const auto check_python = GENERATE(true, false);
  CAPTURE(check_python);
  const auto checked_instance = check_python ? python_instance : cpp_instance;

  REQUIRE(checked_instance.get_varstring_17_field() == input_instance.get_varstring_17_field());
  REQUIRE(checked_instance.get_varstring_27_field() == input_instance.get_varstring_27_field());
  REQUIRE(checked_instance.get_varstring_32_field() == input_instance.get_varstring_32_field());
  REQUIRE(checked_instance.get_varstring_64_field() == input_instance.get_varstring_64_field());
  REQUIRE(checked_instance.get_varstring_16_field() == input_instance.get_varstring_16_field());
  REQUIRE(checked_instance.get_added_varstring_32_field().empty());
  REQUIRE(checked_instance.get_upgraded_vararray_field() == "012");
  REQUIRE(checked_instance.get_upgraded_fixed_array_field() == "abc");
  REQUIRE(checked_instance.get_upgraded_optional_field() == "X");
  REQUIRE(checked_instance.get_upgraded_int8_field() == "A");

  REQUIRE(
    std::ranges::equal(std::as_bytes(std::span{&cpp_instance, 1U}), std::as_bytes(std::span{&python_instance, 1U})));
}

TEST_CASE("Build time upgrade failure")
{
  // Initialize python for the log schema upgrader
  python::python_init(python::InitializationMode::unit_test);

  const auto test_lambda = []()
  {
    // Not expecting this to return
    std::ignore = make_cpp_upgrader<
      Tappy<tests::BuildTimeUpgradeFailureTestSchemaV1>,
      Tappy<tests::BuildTimeUpgradeFailureTestSchemaV2>>();
  };
  REQUIRE_THROWS_WITH(
    test_lambda(),
    Catch::Matchers::StartsWith(
      "Failed to create upgrader for "
      "@clockwork::clockwork::serialization::cpp::tests::support::test_schema_v2::"
      "BuildTimeUpgradeFailureTestSchemaV2.float64_field: "));

  validate_no_upgradability<
    Tappy<tests::BuildTimeUpgradeFailureTestSchemaV1>,
    Tappy<tests::BuildTimeUpgradeFailureTestSchemaV2>>();
}

TEST_CASE("Run time upgrade failure")
{
  // Initialize python for the log schema upgrader
  python::python_init(python::InitializationMode::unit_test);

  const auto cpp_upgrader = make_cpp_upgrader<
    Tappy<tests::RunTimeUpgradeFailureTestSchemaV1>,
    Tappy<tests::RunTimeUpgradeFailureTestSchemaV2>>();
  REQUIRE(cpp_upgrader->upgrade_required());

  Tappy<tests::RunTimeUpgradeFailureTestSchemaV1> input_instance{};
  REQUIRE(input_instance.get_underlying_varstring_field().try_set("012345678901234"));
  Tappy<tests::RunTimeUpgradeFailureTestSchemaV2> cpp_instance{};
  std::memset(&cpp_instance, 0, sizeof(cpp_instance));

  REQUIRE_THROWS_WITH(
    cpp_upgrader->upgrade(
      std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&cpp_instance, 1U})),
    Catch::Matchers::StartsWith(
      "Failed to upgrade "
      "@clockwork::clockwork::serialization::cpp::tests::support::test_schema_v2::"
      "RunTimeUpgradeFailureTestSchemaV2.varstring_field: "));

  validate_upgradability<
    Tappy<tests::RunTimeUpgradeFailureTestSchemaV1>,
    Tappy<tests::RunTimeUpgradeFailureTestSchemaV2>>();
}

// Test types for the upgrade value enum test
using ValueEnumUpgradeTestTypes = std::tuple<
  // Upgrade int8 value enum to all other enum types
  std::tuple<
    int8_t,
    tests::Int8ValueEnumV1,
    tests::Int8ValueEnumUpgradeTestSchemaV1,
    tests::Int8ValueEnumV2,
    tests::Int8ValueEnumUpgradeTestSchemaV2>,
  std::tuple<
    int8_t,
    tests::Int8ValueEnumV1,
    tests::Int8ValueEnumUpgradeTestSchemaV1,
    tests::Int16ValueEnumV2,
    tests::Int16ValueEnumUpgradeTestSchemaV2>,
  std::tuple<
    int8_t,
    tests::Int8ValueEnumV1,
    tests::Int8ValueEnumUpgradeTestSchemaV1,
    tests::Int32ValueEnumV2,
    tests::Int32ValueEnumUpgradeTestSchemaV2>,
  std::tuple<
    int8_t,
    tests::Int8ValueEnumV1,
    tests::Int8ValueEnumUpgradeTestSchemaV1,
    tests::Int64ValueEnumV2,
    tests::Int64ValueEnumUpgradeTestSchemaV2>,
  std::tuple<
    int8_t,
    tests::Int8ValueEnumV1,
    tests::Int8ValueEnumUpgradeTestSchemaV1,
    tests::UInt8ValueEnumV2,
    tests::UInt8ValueEnumUpgradeTestSchemaV2>,
  std::tuple<
    int8_t,
    tests::Int8ValueEnumV1,
    tests::Int8ValueEnumUpgradeTestSchemaV1,
    tests::UInt16ValueEnumV2,
    tests::UInt16ValueEnumUpgradeTestSchemaV2>,
  std::tuple<
    int8_t,
    tests::Int8ValueEnumV1,
    tests::Int8ValueEnumUpgradeTestSchemaV1,
    tests::UInt32ValueEnumV2,
    tests::UInt32ValueEnumUpgradeTestSchemaV2>,
  std::tuple<
    int8_t,
    tests::Int8ValueEnumV1,
    tests::Int8ValueEnumUpgradeTestSchemaV1,
    tests::UInt64ValueEnumV2,
    tests::UInt64ValueEnumUpgradeTestSchemaV2>,
  std::tuple<
    int8_t,
    tests::Int8ValueEnumV1,
    tests::Int8ValueEnumUpgradeTestSchemaV1,
    tests::UInt8BitFlagEnumV2,
    tests::UInt8BitFlagEnumUpgradeTestSchemaV2>,
  std::tuple<
    int8_t,
    tests::Int8ValueEnumV1,
    tests::Int8ValueEnumUpgradeTestSchemaV1,
    tests::UInt16BitFlagEnumV2,
    tests::UInt16BitFlagEnumUpgradeTestSchemaV2>,
  std::tuple<
    int8_t,
    tests::Int8ValueEnumV1,
    tests::Int8ValueEnumUpgradeTestSchemaV1,
    tests::UInt32BitFlagEnumV2,
    tests::UInt32BitFlagEnumUpgradeTestSchemaV2>,
  std::tuple<
    int8_t,
    tests::Int8ValueEnumV1,
    tests::Int8ValueEnumUpgradeTestSchemaV1,
    tests::UInt64BitFlagEnumV2,
    tests::UInt64BitFlagEnumUpgradeTestSchemaV2>,
  // Upgrade all other value enum types to int8 value enum type
  std::tuple<
    int16_t,
    tests::Int16ValueEnumV1,
    tests::Int16ValueEnumUpgradeTestSchemaV1,
    tests::Int8ValueEnumV2,
    tests::Int8ValueEnumUpgradeTestSchemaV2>,
  std::tuple<
    int32_t,
    tests::Int32ValueEnumV1,
    tests::Int32ValueEnumUpgradeTestSchemaV1,
    tests::Int8ValueEnumV2,
    tests::Int8ValueEnumUpgradeTestSchemaV2>,
  std::tuple<
    int64_t,
    tests::Int64ValueEnumV1,
    tests::Int64ValueEnumUpgradeTestSchemaV1,
    tests::Int8ValueEnumV2,
    tests::Int8ValueEnumUpgradeTestSchemaV2>,
  std::tuple<
    uint8_t,
    tests::UInt8ValueEnumV1,
    tests::UInt8ValueEnumUpgradeTestSchemaV1,
    tests::Int8ValueEnumV2,
    tests::Int8ValueEnumUpgradeTestSchemaV2>,
  std::tuple<
    uint16_t,
    tests::UInt16ValueEnumV1,
    tests::UInt16ValueEnumUpgradeTestSchemaV1,
    tests::Int8ValueEnumV2,
    tests::Int8ValueEnumUpgradeTestSchemaV2>,
  std::tuple<
    uint32_t,
    tests::UInt32ValueEnumV1,
    tests::UInt32ValueEnumUpgradeTestSchemaV1,
    tests::Int8ValueEnumV2,
    tests::Int8ValueEnumUpgradeTestSchemaV2>,
  std::tuple<
    uint64_t,
    tests::UInt64ValueEnumV1,
    tests::UInt64ValueEnumUpgradeTestSchemaV1,
    tests::Int8ValueEnumV2,
    tests::Int8ValueEnumUpgradeTestSchemaV2>>;

TEMPLATE_LIST_TEST_CASE("Value enum upgrade", "", ValueEnumUpgradeTestTypes)
{
  using SrcValueType = std::tuple_element_t<0U, TestType>;
  using SrcEnumType = std::tuple_element_t<1U, TestType>;
  using SrcSchemaType = std::tuple_element_t<2U, TestType>;
  using DestEnumType = std::tuple_element_t<3U, TestType>;
  using DestSchemaType = std::tuple_element_t<4U, TestType>;

  CAPTURE(typeid(SrcEnumType).name());
  CAPTURE(typeid(DestEnumType).name());

  // Initialize python for the log schema upgrader
  python::python_init(python::InitializationMode::unit_test);

  const auto python_upgrader = make_python_upgrader<Tappy<SrcSchemaType>, Tappy<DestSchemaType>>();
  REQUIRE(python_upgrader->upgrade_required());

  const auto cpp_upgrader = make_cpp_upgrader<Tappy<SrcSchemaType>, Tappy<DestSchemaType>>();
  REQUIRE(cpp_upgrader->upgrade_required());

  validate_upgradability<Tappy<SrcSchemaType>, Tappy<DestSchemaType>>();

  Tappy<SrcSchemaType> input_instance{};
  input_instance.set_field2(SrcEnumType::value1);
  input_instance.set_field3(SrcEnumType::value2);
  input_instance.set_field4(SrcEnumType::value3);
  input_instance.set_field5(SrcEnumType::value4);
  input_instance.set_field6(static_cast<SrcValueType>(DestEnumType::value0));
  input_instance.set_field7(static_cast<SrcValueType>(DestEnumType::value1));
  input_instance.set_field9(static_cast<SrcValueType>(DestEnumType::value3_1));
  input_instance.set_field10(static_cast<SrcValueType>(DestEnumType::value4));
  Tappy<DestSchemaType> python_instance{};
  Tappy<DestSchemaType> cpp_instance{};
  std::memset(&python_instance, 0, sizeof(python_instance));
  std::memset(&cpp_instance, 0, sizeof(cpp_instance));

  python_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&python_instance, 1U}));
  cpp_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&cpp_instance, 1U}));

  const auto check_python = GENERATE(true, false);
  CAPTURE(check_python);
  const auto checked_instance = check_python ? python_instance : cpp_instance;

  REQUIRE(checked_instance.get_field1() == DestEnumType::value0);
  REQUIRE(checked_instance.get_field2() == DestEnumType::value1);
  REQUIRE(checked_instance.get_field4() == DestEnumType::value3_1);
  REQUIRE(checked_instance.get_field5() == DestEnumType::value4);
  REQUIRE(checked_instance.get_field11() == DestEnumType::value5);
  REQUIRE(checked_instance.get_field12() == DestEnumType::value0);
  REQUIRE(checked_instance.get_field13() == DestEnumType::value1);
  REQUIRE(checked_instance.get_field14() == DestEnumType::value3_1);
  REQUIRE(checked_instance.get_field15() == DestEnumType::value4);

  REQUIRE(
    std::ranges::equal(std::as_bytes(std::span{&cpp_instance, 1U}), std::as_bytes(std::span{&python_instance, 1U})));

  input_instance.set_field1(SrcEnumType::value2); // Removed in V2
  REQUIRE_THROWS_WITH(
    cpp_upgrader->upgrade(
      std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&cpp_instance, 1U})),
    Catch::Matchers::ContainsSubstring("field1: Invalid enum value (2)"));
}

// Test types for the upgrade bit-flag enum to value enum test
using BitFlagToValueEnumUpgradeTestTypes = std::tuple<
  // Upgrade uint8 bit-flag enum to all value enum types
  std::tuple<
    uint8_t,
    tests::UInt8BitFlagEnumV1,
    tests::UInt8BitFlagEnumUpgradeTestSchemaV1,
    tests::Int8ValueEnumV2,
    tests::Int8ValueEnumUpgradeTestSchemaV2>,
  std::tuple<
    uint8_t,
    tests::UInt8BitFlagEnumV1,
    tests::UInt8BitFlagEnumUpgradeTestSchemaV1,
    tests::Int16ValueEnumV2,
    tests::Int16ValueEnumUpgradeTestSchemaV2>,
  std::tuple<
    uint8_t,
    tests::UInt8BitFlagEnumV1,
    tests::UInt8BitFlagEnumUpgradeTestSchemaV1,
    tests::Int32ValueEnumV2,
    tests::Int32ValueEnumUpgradeTestSchemaV2>,
  std::tuple<
    uint8_t,
    tests::UInt8BitFlagEnumV1,
    tests::UInt8BitFlagEnumUpgradeTestSchemaV1,
    tests::Int64ValueEnumV2,
    tests::Int64ValueEnumUpgradeTestSchemaV2>,
  std::tuple<
    uint8_t,
    tests::UInt8BitFlagEnumV1,
    tests::UInt8BitFlagEnumUpgradeTestSchemaV1,
    tests::UInt8ValueEnumV2,
    tests::UInt8ValueEnumUpgradeTestSchemaV2>,
  std::tuple<
    uint8_t,
    tests::UInt8BitFlagEnumV1,
    tests::UInt8BitFlagEnumUpgradeTestSchemaV1,
    tests::UInt16ValueEnumV2,
    tests::UInt16ValueEnumUpgradeTestSchemaV2>,
  std::tuple<
    uint8_t,
    tests::UInt8BitFlagEnumV1,
    tests::UInt8BitFlagEnumUpgradeTestSchemaV1,
    tests::UInt32ValueEnumV2,
    tests::UInt32ValueEnumUpgradeTestSchemaV2>,
  std::tuple<
    uint8_t,
    tests::UInt8BitFlagEnumV1,
    tests::UInt8BitFlagEnumUpgradeTestSchemaV1,
    tests::UInt64ValueEnumV2,
    tests::UInt64ValueEnumUpgradeTestSchemaV2>,
  // Upgrade all unsigned bit-flag enum to int8 value enum type
  std::tuple<
    uint16_t,
    tests::UInt16BitFlagEnumV1,
    tests::UInt16BitFlagEnumUpgradeTestSchemaV1,
    tests::Int8ValueEnumV2,
    tests::Int8ValueEnumUpgradeTestSchemaV2>,
  std::tuple<
    uint32_t,
    tests::UInt32BitFlagEnumV1,
    tests::UInt32BitFlagEnumUpgradeTestSchemaV1,
    tests::Int8ValueEnumV2,
    tests::Int8ValueEnumUpgradeTestSchemaV2>,
  std::tuple<
    uint64_t,
    tests::UInt64BitFlagEnumV1,
    tests::UInt64BitFlagEnumUpgradeTestSchemaV1,
    tests::Int8ValueEnumV2,
    tests::Int8ValueEnumUpgradeTestSchemaV2>>;

TEMPLATE_LIST_TEST_CASE("Bit-flag enum to value enum upgrade", "", BitFlagToValueEnumUpgradeTestTypes)
{
  using SrcValueType = std::tuple_element_t<0U, TestType>;
  using SrcEnumType = std::tuple_element_t<1U, TestType>;
  using SrcSchemaType = std::tuple_element_t<2U, TestType>;
  using DestEnumType = std::tuple_element_t<3U, TestType>;
  using DestSchemaType = std::tuple_element_t<4U, TestType>;

  CAPTURE(typeid(SrcEnumType).name());
  CAPTURE(typeid(DestEnumType).name());

  // Initialize python for the log schema upgrader
  python::python_init(python::InitializationMode::unit_test);

  const auto python_upgrader = make_python_upgrader<Tappy<SrcSchemaType>, Tappy<DestSchemaType>>();
  REQUIRE(python_upgrader->upgrade_required());

  const auto cpp_upgrader = make_cpp_upgrader<Tappy<SrcSchemaType>, Tappy<DestSchemaType>>();
  REQUIRE(cpp_upgrader->upgrade_required());

  validate_upgradability<Tappy<SrcSchemaType>, Tappy<DestSchemaType>>();

  Tappy<SrcSchemaType> input_instance{};
  input_instance.set_field2(SrcEnumType::value1);
  input_instance.set_field3(SrcEnumType::value2);
  input_instance.set_field4(SrcEnumType::value3);
  input_instance.set_field5(SrcEnumType::value4);
  input_instance.set_field6(static_cast<SrcValueType>(DestEnumType::value0));
  input_instance.set_field7(static_cast<SrcValueType>(DestEnumType::value1));
  input_instance.set_field9(static_cast<SrcValueType>(DestEnumType::value3_1));
  input_instance.set_field10(static_cast<SrcValueType>(DestEnumType::value4));
  Tappy<DestSchemaType> python_instance{};
  Tappy<DestSchemaType> cpp_instance{};
  std::memset(&python_instance, 0, sizeof(python_instance));
  std::memset(&cpp_instance, 0, sizeof(cpp_instance));

  python_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&python_instance, 1U}));
  cpp_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&cpp_instance, 1U}));

  const auto check_python = GENERATE(true, false);
  CAPTURE(check_python);
  const auto checked_instance = check_python ? python_instance : cpp_instance;

  REQUIRE(checked_instance.get_field1() == DestEnumType::value0);
  REQUIRE(checked_instance.get_field2() == DestEnumType::value1);
  REQUIRE(checked_instance.get_field4() == DestEnumType::value3_1);
  REQUIRE(checked_instance.get_field5() == DestEnumType::value4);
  REQUIRE(checked_instance.get_field11() == DestEnumType::value5);
  REQUIRE(checked_instance.get_field12() == DestEnumType::value0);
  REQUIRE(checked_instance.get_field13() == DestEnumType::value1);
  REQUIRE(checked_instance.get_field14() == DestEnumType::value3_1);
  REQUIRE(checked_instance.get_field15() == DestEnumType::value4);

  REQUIRE(
    std::ranges::equal(std::as_bytes(std::span{&cpp_instance, 1U}), std::as_bytes(std::span{&python_instance, 1U})));

  input_instance.set_field1(SrcEnumType::value2); // Removed in V2
  REQUIRE_THROWS_WITH(
    cpp_upgrader->upgrade(
      std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&cpp_instance, 1U})),
    Catch::Matchers::ContainsSubstring("field1: Invalid enum bit flags (0x2)"));

  input_instance.set_field1(SrcEnumType::value3 + SrcEnumType::value4); // Ambiguous upgrade
  REQUIRE_THROWS_WITH(
    cpp_upgrader->upgrade(
      std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&cpp_instance, 1U})),
    Catch::Matchers::ContainsSubstring("field1: Ambiguous enum bit flags (0xc)"));
}

// Test types for the upgrade bit-flag enum to bit-flag enum test
using BitFlagToBitFlagEnumUpgradeTestTypes = std::tuple<
  // Upgrade uint8 bit-flag enum to all unsigned bit-flag enum types
  std::tuple<
    uint8_t,
    tests::UInt8BitFlagEnumV1,
    tests::UInt8BitFlagEnumUpgradeTestSchemaV1,
    tests::UInt8BitFlagEnumV2,
    tests::UInt8BitFlagEnumUpgradeTestSchemaV2>,
  std::tuple<
    uint8_t,
    tests::UInt8BitFlagEnumV1,
    tests::UInt8BitFlagEnumUpgradeTestSchemaV1,
    tests::UInt16BitFlagEnumV2,
    tests::UInt16BitFlagEnumUpgradeTestSchemaV2>,
  std::tuple<
    uint8_t,
    tests::UInt8BitFlagEnumV1,
    tests::UInt8BitFlagEnumUpgradeTestSchemaV1,
    tests::UInt32BitFlagEnumV2,
    tests::UInt32BitFlagEnumUpgradeTestSchemaV2>,
  std::tuple<
    uint8_t,
    tests::UInt8BitFlagEnumV1,
    tests::UInt8BitFlagEnumUpgradeTestSchemaV1,
    tests::UInt64BitFlagEnumV2,
    tests::UInt64BitFlagEnumUpgradeTestSchemaV2>,
  // Upgrade all unsigned bit-flag enum to uint8 bit-flag enum type
  std::tuple<
    uint16_t,
    tests::UInt16BitFlagEnumV1,
    tests::UInt16BitFlagEnumUpgradeTestSchemaV1,
    tests::UInt8BitFlagEnumV2,
    tests::UInt8BitFlagEnumUpgradeTestSchemaV2>,
  std::tuple<
    uint32_t,
    tests::UInt32BitFlagEnumV1,
    tests::UInt32BitFlagEnumUpgradeTestSchemaV1,
    tests::UInt8BitFlagEnumV2,
    tests::UInt8BitFlagEnumUpgradeTestSchemaV2>,
  std::tuple<
    uint64_t,
    tests::UInt64BitFlagEnumV1,
    tests::UInt64BitFlagEnumUpgradeTestSchemaV1,
    tests::UInt8BitFlagEnumV2,
    tests::UInt8BitFlagEnumUpgradeTestSchemaV2>>;

TEMPLATE_LIST_TEST_CASE("Bit-flag enum to bit-flag enum upgrade", "", BitFlagToBitFlagEnumUpgradeTestTypes)
{
  using SrcValueType = std::tuple_element_t<0U, TestType>;
  using SrcEnumType = std::tuple_element_t<1U, TestType>;
  using SrcSchemaType = std::tuple_element_t<2U, TestType>;
  using DestEnumType = std::tuple_element_t<3U, TestType>;
  using DestSchemaType = std::tuple_element_t<4U, TestType>;

  CAPTURE(typeid(SrcEnumType).name());
  CAPTURE(typeid(DestEnumType).name());

  // Initialize python for the log schema upgrader
  python::python_init(python::InitializationMode::unit_test);

  const auto python_upgrader = make_python_upgrader<Tappy<SrcSchemaType>, Tappy<DestSchemaType>>();
  REQUIRE(python_upgrader->upgrade_required());

  const auto cpp_upgrader = make_cpp_upgrader<Tappy<SrcSchemaType>, Tappy<DestSchemaType>>();
  REQUIRE(cpp_upgrader->upgrade_required());

  validate_upgradability<Tappy<SrcSchemaType>, Tappy<DestSchemaType>>();

  Tappy<SrcSchemaType> input_instance{};
  input_instance.set_field2(SrcEnumType::value1);
  input_instance.set_field3(SrcEnumType::value2);
  input_instance.set_field4(SrcEnumType::value3);
  input_instance.set_field5(SrcEnumType::value4);
  input_instance.set_field6(static_cast<SrcValueType>(DestEnumType::value0));
  input_instance.set_field7(static_cast<SrcValueType>(DestEnumType::value1));
  input_instance.set_field9(static_cast<SrcValueType>(DestEnumType::value3_1));
  input_instance.set_field10(static_cast<SrcValueType>(DestEnumType::value4));
  Tappy<DestSchemaType> python_instance{};
  Tappy<DestSchemaType> cpp_instance{};
  std::memset(&python_instance, 0, sizeof(python_instance));
  std::memset(&cpp_instance, 0, sizeof(cpp_instance));

  python_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&python_instance, 1U}));
  cpp_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&cpp_instance, 1U}));

  const auto check_python = GENERATE(true, false);
  CAPTURE(check_python);
  const auto checked_instance = check_python ? python_instance : cpp_instance;

  REQUIRE(checked_instance.get_field1() == DestEnumType::value0);
  REQUIRE(checked_instance.get_field2() == DestEnumType::value1);
  REQUIRE(checked_instance.get_field4() == DestEnumType::value3_1);
  REQUIRE(checked_instance.get_field5() == DestEnumType::value4);
  REQUIRE(checked_instance.get_field11() == DestEnumType::value5);
  REQUIRE(checked_instance.get_field12() == DestEnumType::value0);
  REQUIRE(checked_instance.get_field13() == DestEnumType::value1);
  REQUIRE(checked_instance.get_field14() == DestEnumType::value3_1);
  REQUIRE(checked_instance.get_field15() == DestEnumType::value4);

  REQUIRE(
    std::ranges::equal(std::as_bytes(std::span{&cpp_instance, 1U}), std::as_bytes(std::span{&python_instance, 1U})));

  input_instance.set_field1(SrcEnumType::value2); // Removed in V2
  REQUIRE_THROWS_WITH(
    cpp_upgrader->upgrade(
      std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&cpp_instance, 1U})),
    Catch::Matchers::ContainsSubstring("field1: Invalid enum bit flags (0x2)"));
}

TEST_CASE("FixedSoa upgrade with field evolution")
{
  // Test upgrading FixedSoa from V1 to V2 where the element schema has evolved
  using SrcSchemaType = tests::FixedSoaUpgradeTestSchemaV1;
  using DestSchemaType = tests::FixedSoaUpgradeTestSchemaV2;

  const auto cpp_upgrader = make_cpp_upgrader<Tappy<SrcSchemaType>, Tappy<DestSchemaType>>();
  REQUIRE(cpp_upgrader->upgrade_required());

  // Create V1 instance with 5 elements
  Tappy<SrcSchemaType> input_instance{};
  auto& src_soa = input_instance.get_mutable_soa_field();

  // Initialize 5 elements in the FixedSoa
  for (size_t i = 0; i < 5; ++i)
  {
    src_soa.view_id()[i] = static_cast<int32_t>(100 + i);
    src_soa.view_x()[i] = static_cast<int32_t>(i + 1);
    src_soa.view_y()[i] = static_cast<float>(static_cast<double>(i) + 1.5);
    src_soa.view_z()[i] = static_cast<float>(static_cast<double>(i + 1) * 10.0);

    // Initialize nested schema member
    auto& nested = src_soa.view_nested()[i];
    nested.set_removed_field(static_cast<int8_t>(i));
    nested.set_upgraded_field(static_cast<uint32_t>(1000 + i));

    // Initialize VarArray of schemas
    auto& items = src_soa.view_items()[i];
    auto& item0 = items.emplace_back();
    item0.set_removed_field(static_cast<int8_t>(10 + i));
    item0.set_upgraded_field(static_cast<uint32_t>(2000 + i));
    auto& item1 = items.emplace_back();
    item1.set_removed_field(static_cast<int8_t>(20 + i));
    item1.set_upgraded_field(static_cast<uint32_t>(3000 + i));
  }

  // Upgrade to V2
  Tappy<DestSchemaType> cpp_instance{};
  std::memset(&cpp_instance, 0, sizeof(cpp_instance));

  cpp_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&cpp_instance, 1U}));

  // Verify upgraded values
  const auto& dest_soa = cpp_instance.get_soa_field();

  for (size_t i = 0; i < 5; ++i)
  {
    CAPTURE(i);

    // ID field unchanged
    REQUIRE(dest_soa.view_id()[i] == static_cast<int32_t>(100 + i));

    // X field upgraded from Int32 to Int64
    REQUIRE(dest_soa.view_x()[i] == static_cast<int64_t>(i + 1));

    // Y field upgraded from Float32 to Float64
    REQUIRE(dest_soa.view_y()[i] == Catch::Approx(static_cast<double>(i) + 1.5));

    // Z field was removed, W field is new with default value 0.0
    REQUIRE(dest_soa.view_w()[i] == 0.0f);

    // Verify nested schema member upgraded correctly
    const auto& nested = dest_soa.view_nested()[i];
    REQUIRE(nested.get_upgraded_field() == static_cast<uint64_t>(1000 + i)); // UInt32 -> UInt64

    // Verify VarArray of schemas upgraded correctly
    const auto& items = dest_soa.view_items()[i];
    REQUIRE(items.size() == 2);
    REQUIRE(items[0].get_upgraded_field() == static_cast<uint64_t>(2000 + i));
    REQUIRE(items[1].get_upgraded_field() == static_cast<uint64_t>(3000 + i));
  }
}

TEST_CASE("VarSoa upgrade with field evolution")
{
  // Test upgrading VarSoa from V1 to V2 where the element schema has evolved
  using SrcSchemaType = tests::VarSoaUpgradeTestSchemaV1;
  using DestSchemaType = tests::VarSoaUpgradeTestSchemaV2;

  const auto cpp_upgrader = make_cpp_upgrader<Tappy<SrcSchemaType>, Tappy<DestSchemaType>>();
  REQUIRE(cpp_upgrader->upgrade_required());

  SECTION("Empty VarSoa")
  {
    // Test upgrading an empty VarSoa
    Tappy<SrcSchemaType> input_instance{};
    auto& src_soa = input_instance.get_mutable_soa_field();
    REQUIRE(jewels::ok(src_soa.try_resize(0)));

    Tappy<DestSchemaType> cpp_instance{};
    std::memset(&cpp_instance, 0, sizeof(cpp_instance));

    cpp_upgrader->upgrade(
      std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&cpp_instance, 1U}));

    const auto& dest_soa = cpp_instance.get_soa_field();
    REQUIRE(dest_soa.size() == 0);
  }

  SECTION("VarSoa with 3 elements")
  {
    // Test upgrading a VarSoa with 3 elements
    Tappy<SrcSchemaType> input_instance{};
    auto& src_soa = input_instance.get_mutable_soa_field();
    REQUIRE(jewels::ok(src_soa.try_resize(3)));

    // Initialize 3 elements
    for (size_t i = 0; i < 3; ++i)
    {
      src_soa.view_id()[i] = static_cast<int32_t>(200 + i);
      src_soa.view_x()[i] = static_cast<int32_t>((i + 1) * 10);
      src_soa.view_y()[i] = static_cast<float>(static_cast<double>(i + 1) * 1.1);
      src_soa.view_z()[i] = static_cast<float>(static_cast<double>(i + 1) * 100.0);

      // Initialize nested schema
      auto& nested = src_soa.view_nested()[i];
      nested.set_removed_field(static_cast<int8_t>(i * 2));
      nested.set_upgraded_field(static_cast<uint32_t>(5000 + (i * 10)));

      // Initialize VarArray with varying sizes
      auto& items = src_soa.view_items()[i];
      const size_t num_items = i + 1; // Element 0 has 1 item, element 1 has 2, etc.
      for (size_t j = 0; j < num_items; ++j)
      {
        auto& item = items.emplace_back();
        item.set_removed_field(static_cast<int8_t>((i * 10) + j));
        item.set_upgraded_field(static_cast<uint32_t>(6000 + (i * 100) + j));
      }
    }

    // Upgrade to V2
    Tappy<DestSchemaType> cpp_instance{};
    std::memset(&cpp_instance, 0, sizeof(cpp_instance));

    cpp_upgrader->upgrade(
      std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&cpp_instance, 1U}));

    // Verify upgraded values
    const auto& dest_soa = cpp_instance.get_soa_field();
    REQUIRE(dest_soa.size() == 3);

    for (size_t i = 0; i < 3; ++i)
    {
      CAPTURE(i);

      // ID field unchanged
      REQUIRE(dest_soa.view_id()[i] == static_cast<int32_t>(200 + i));

      // X field upgraded from Int32 to Int64
      REQUIRE(dest_soa.view_x()[i] == static_cast<int64_t>((i + 1) * 10));

      // Y field upgraded from Float32 to Float64
      REQUIRE(dest_soa.view_y()[i] == Catch::Approx(static_cast<double>(i + 1) * 1.1));

      // W field is new with default value 0.0
      REQUIRE(dest_soa.view_w()[i] == 0.0f);

      // Verify nested schema upgraded
      const auto& nested = dest_soa.view_nested()[i];
      REQUIRE(nested.get_upgraded_field() == static_cast<uint64_t>(5000 + (i * 10)));

      // Verify VarArray upgraded with correct sizes
      const auto& items = dest_soa.view_items()[i];
      const size_t expected_size = i + 1;
      REQUIRE(items.size() == expected_size);
      for (size_t j = 0; j < expected_size; ++j)
      {
        CAPTURE(j);
        REQUIRE(items[j].get_upgraded_field() == static_cast<uint64_t>(6000 + (i * 100) + j));
      }
    }
  }

  SECTION("VarSoa with max elements")
  {
    // Test upgrading a VarSoa with maximum elements (10)
    Tappy<SrcSchemaType> input_instance{};
    auto& src_soa = input_instance.get_mutable_soa_field();
    REQUIRE(jewels::ok(src_soa.try_resize(10)));

    // Initialize all 10 elements
    for (size_t i = 0; i < 10; ++i)
    {
      src_soa.view_id()[i] = static_cast<int32_t>(1000 + i);
      src_soa.view_x()[i] = static_cast<int32_t>(i * 10);
      src_soa.view_y()[i] = static_cast<float>(static_cast<double>(i) * 1.5);
      src_soa.view_z()[i] = static_cast<float>(static_cast<double>(i) * 100.0);
    }

    // Upgrade to V2
    Tappy<DestSchemaType> cpp_instance{};
    std::memset(&cpp_instance, 0, sizeof(cpp_instance));

    cpp_upgrader->upgrade(
      std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&cpp_instance, 1U}));

    // Verify all upgraded values
    const auto& dest_soa = cpp_instance.get_soa_field();
    REQUIRE(dest_soa.size() == 10);

    for (size_t i = 0; i < 10; ++i)
    {
      CAPTURE(i);
      REQUIRE(dest_soa.view_id()[i] == static_cast<int32_t>(1000 + i));
      REQUIRE(dest_soa.view_x()[i] == static_cast<int64_t>(i * 10));
      REQUIRE(dest_soa.view_y()[i] == Catch::Approx(static_cast<double>(i) * 1.5));
      REQUIRE(dest_soa.view_w()[i] == 0.0f);
    }
  }
}

TEST_CASE("FixedSoa to VarSoa conversion")
{
  // Test converting FixedSoa<3> to VarSoa<max_size=10>
  using SrcSchemaType = tests::FixedToVarSoaConversionTestSchemaV1;
  using DestSchemaType = tests::FixedToVarSoaConversionTestSchemaV2;

  const auto cpp_upgrader = make_cpp_upgrader<Tappy<SrcSchemaType>, Tappy<DestSchemaType>>();
  REQUIRE(cpp_upgrader->upgrade_required());

  // Create V1 instance with FixedSoa of 3 elements
  Tappy<SrcSchemaType> input_instance{};
  auto& src_soa = input_instance.get_mutable_soa_field();

  // Initialize all 3 elements in the FixedSoa
  for (size_t i = 0; i < 3; ++i)
  {
    src_soa.view_id()[i] = static_cast<int32_t>(300 + i);
    src_soa.view_x()[i] = static_cast<int32_t>((i + 1) * 7);
    src_soa.view_y()[i] = static_cast<float>(static_cast<double>(i + 1) * 0.7);
    src_soa.view_z()[i] = static_cast<float>(static_cast<double>(i + 1) * 70.0);

    auto& nested = src_soa.view_nested()[i];
    nested.set_removed_field(static_cast<int8_t>(i + 5));
    nested.set_upgraded_field(static_cast<uint32_t>(7000 + (i * 10)));

    auto& items = src_soa.view_items()[i];
    auto& item = items.emplace_back();
    item.set_removed_field(static_cast<int8_t>(i + 50));
    item.set_upgraded_field(static_cast<uint32_t>(8000 + (i * 10)));
  }

  // Upgrade to V2 (FixedSoa -> VarSoa)
  Tappy<DestSchemaType> cpp_instance{};
  std::memset(&cpp_instance, 0, sizeof(cpp_instance));

  cpp_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&cpp_instance, 1U}));

  // Verify converted to VarSoa with correct size
  const auto& dest_soa = cpp_instance.get_soa_field();
  REQUIRE(dest_soa.size() == 3); // Should have 3 elements from FixedSoa

  for (size_t i = 0; i < 3; ++i)
  {
    CAPTURE(i);

    // Verify all fields upgraded correctly
    REQUIRE(dest_soa.view_id()[i] == static_cast<int32_t>(300 + i));
    REQUIRE(dest_soa.view_x()[i] == static_cast<int64_t>((i + 1) * 7));
    REQUIRE(dest_soa.view_y()[i] == Catch::Approx(static_cast<double>(i + 1) * 0.7));
    REQUIRE(dest_soa.view_w()[i] == 0.0f);

    const auto& nested = dest_soa.view_nested()[i];
    REQUIRE(nested.get_upgraded_field() == static_cast<uint64_t>(7000 + (i * 10)));

    const auto& items = dest_soa.view_items()[i];
    REQUIRE(items.size() == 1);
    REQUIRE(items[0].get_upgraded_field() == static_cast<uint64_t>(8000 + (i * 10)));
  }
}

TEST_CASE("VarSoa to FixedSoa conversion")
{
  // Test converting VarSoa with 3 elements to FixedSoa<3>
  using SrcSchemaType = tests::VarToFixedSoaConversionTestSchemaV1;
  using DestSchemaType = tests::VarToFixedSoaConversionTestSchemaV2;

  const auto cpp_upgrader = make_cpp_upgrader<Tappy<SrcSchemaType>, Tappy<DestSchemaType>>();
  REQUIRE(cpp_upgrader->upgrade_required());

  SECTION("VarSoa with 3 elements converts to FixedSoa<3>")
  {
    // Create V1 instance with VarSoa of 3 elements
    Tappy<SrcSchemaType> input_instance{};
    auto& src_soa = input_instance.get_mutable_soa_field();
    REQUIRE(jewels::ok(src_soa.try_resize(3)));

    // Initialize 3 elements
    for (size_t i = 0; i < 3; ++i)
    {
      src_soa.view_id()[i] = static_cast<int32_t>(400 + i);
      src_soa.view_x()[i] = static_cast<int32_t>((i + 1) * 8);
      src_soa.view_y()[i] = static_cast<float>(static_cast<double>(i + 1) * 0.8);
      src_soa.view_z()[i] = static_cast<float>(static_cast<double>(i + 1) * 80.0);

      auto& nested = src_soa.view_nested()[i];
      nested.set_removed_field(static_cast<int8_t>(i + 6));
      nested.set_upgraded_field(static_cast<uint32_t>(9000 + (i * 10)));

      auto& items = src_soa.view_items()[i];
      auto& item = items.emplace_back();
      item.set_removed_field(static_cast<int8_t>(i + 60));
      item.set_upgraded_field(static_cast<uint32_t>(10000 + (i * 10)));
    }

    // Upgrade to V2 (VarSoa -> FixedSoa)
    Tappy<DestSchemaType> cpp_instance{};
    std::memset(&cpp_instance, 0, sizeof(cpp_instance));

    cpp_upgrader->upgrade(
      std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&cpp_instance, 1U}));

    // Verify converted to FixedSoa with all 3 elements
    const auto& dest_soa = cpp_instance.get_soa_field();

    for (size_t i = 0; i < 3; ++i)
    {
      CAPTURE(i);

      // Verify all fields upgraded correctly
      REQUIRE(dest_soa.view_id()[i] == static_cast<int32_t>(400 + i));
      REQUIRE(dest_soa.view_x()[i] == static_cast<int64_t>((i + 1) * 8));
      REQUIRE(dest_soa.view_y()[i] == Catch::Approx(static_cast<double>(i + 1) * 0.8));
      REQUIRE(dest_soa.view_w()[i] == 0.0f);

      const auto& nested = dest_soa.view_nested()[i];
      REQUIRE(nested.get_upgraded_field() == static_cast<uint64_t>(9000 + (i * 10)));

      const auto& items = dest_soa.view_items()[i];
      REQUIRE(items.size() == 1);
      REQUIRE(items[0].get_upgraded_field() == static_cast<uint64_t>(10000 + (i * 10)));
    }
  }

  SECTION("VarSoa with fewer elements converts to FixedSoa<3>")
  {
    // Create V1 instance with VarSoa of only 2 elements
    Tappy<SrcSchemaType> input_instance{};
    auto& src_soa = input_instance.get_mutable_soa_field();
    REQUIRE(jewels::ok(src_soa.try_resize(2)));

    // Initialize 2 elements
    for (size_t i = 0; i < 2; ++i)
    {
      src_soa.view_id()[i] = static_cast<int32_t>(500 + i);
      src_soa.view_x()[i] = static_cast<int32_t>((i + 1) * 9);
      src_soa.view_y()[i] = static_cast<float>(static_cast<double>(i + 1) * 0.9);
      src_soa.view_z()[i] = static_cast<float>(static_cast<double>(i + 1) * 90.0);

      auto& nested = src_soa.view_nested()[i];
      nested.set_removed_field(static_cast<int8_t>(i + 7));
      nested.set_upgraded_field(static_cast<uint32_t>(11000 + (i * 10)));

      auto& items = src_soa.view_items()[i];
      auto& item = items.emplace_back();
      item.set_removed_field(static_cast<int8_t>(i + 70));
      item.set_upgraded_field(static_cast<uint32_t>(12000 + (i * 10)));
    }

    // Upgrade to V2 (VarSoa with 2 elements -> FixedSoa<3>)
    Tappy<DestSchemaType> cpp_instance{};
    std::memset(&cpp_instance, 0, sizeof(cpp_instance));

    cpp_upgrader->upgrade(
      std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&cpp_instance, 1U}));

    // Verify converted to FixedSoa - first 2 elements have data, 3rd has defaults
    const auto& dest_soa = cpp_instance.get_soa_field();

    for (size_t i = 0; i < 2; ++i)
    {
      CAPTURE(i);

      // Verify upgraded data
      REQUIRE(dest_soa.view_id()[i] == static_cast<int32_t>(500 + i));
      REQUIRE(dest_soa.view_x()[i] == static_cast<int64_t>((i + 1) * 9));
      REQUIRE(dest_soa.view_y()[i] == Catch::Approx(static_cast<double>(i + 1) * 0.9));
      REQUIRE(dest_soa.view_w()[i] == 0.0f);

      const auto& nested = dest_soa.view_nested()[i];
      REQUIRE(nested.get_upgraded_field() == static_cast<uint64_t>(11000 + (i * 10)));

      const auto& items = dest_soa.view_items()[i];
      REQUIRE(items.size() == 1);
      REQUIRE(items[0].get_upgraded_field() == static_cast<uint64_t>(12000 + (i * 10)));
    }

    // Third element should have default values
    REQUIRE(dest_soa.view_id()[2] == 0);
    REQUIRE(dest_soa.view_x()[2] == 0);
    REQUIRE(dest_soa.view_w()[2] == 0.0f);
  }
}

TEST_CASE("FixedArray to FixedSoa transposition")
{
  // Test upgrading FixedArray<Schema> to FixedSoa<Schema>
  using SrcSchemaType = tests::FixedArrayToFixedSoaTestSchemaV1;
  using DestSchemaType = tests::FixedArrayToFixedSoaTestSchemaV2;

  const auto cpp_upgrader = make_cpp_upgrader<Tappy<SrcSchemaType>, Tappy<DestSchemaType>>();
  REQUIRE(cpp_upgrader->upgrade_required());

  // Create V1 instance with FixedArray of 3 elements
  Tappy<SrcSchemaType> input_instance{};
  auto src_array = input_instance.get_mutable_array_field();

  // Initialize 3 elements in the FixedArray
  for (size_t i = 0; i < 3; ++i)
  {
    src_array[i].set_id(static_cast<int32_t>(100 + i));
    src_array[i].set_x(static_cast<int32_t>((i + 1) * 10));
    src_array[i].set_y(static_cast<float>(static_cast<double>(i + 1) * 1.5));
    src_array[i].set_z(static_cast<float>(static_cast<double>(i + 1) * 100.0));

    // Initialize nested schema member (which supports soa_enabled accessor pattern)
    src_array[i].get_mutable_nested().set_removed_field(static_cast<int8_t>(i));
    src_array[i].get_mutable_nested().set_upgraded_field(static_cast<uint32_t>(1000 + i));
  }

  // Upgrade to V2 (FixedArray -> FixedSoa)
  Tappy<DestSchemaType> cpp_instance{};
  std::memset(&cpp_instance, 0, sizeof(cpp_instance));

  cpp_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&cpp_instance, 1U}));

  // Verify transposed to FixedSoa
  const auto& dest_soa = cpp_instance.get_array_field();

  for (size_t i = 0; i < 3; ++i)
  {
    CAPTURE(i);

    // ID field unchanged
    REQUIRE(dest_soa.view_id()[i] == static_cast<int32_t>(100 + i));

    // X field upgraded from Int32 to Int64
    REQUIRE(dest_soa.view_x()[i] == static_cast<int64_t>((i + 1) * 10));

    // Y field upgraded from Float32 to Float64
    REQUIRE(dest_soa.view_y()[i] == Catch::Approx(static_cast<double>(i + 1) * 1.5));

    // W field is new with default value 0.0
    REQUIRE(dest_soa.view_w()[i] == 0.0f);

    // Verify nested schema member upgraded correctly
    const auto& nested = dest_soa.view_nested()[i];
    REQUIRE(nested.get_upgraded_field() == static_cast<uint64_t>(1000 + i));
  }
}

TEST_CASE("VarArray to VarSoa transposition")
{
  // Test upgrading VarArray<Schema> to VarSoa<Schema>
  using SrcSchemaType = tests::VarArrayToVarSoaTestSchemaV1;
  using DestSchemaType = tests::VarArrayToVarSoaTestSchemaV2;

  const auto cpp_upgrader = make_cpp_upgrader<Tappy<SrcSchemaType>, Tappy<DestSchemaType>>();
  REQUIRE(cpp_upgrader->upgrade_required());

  SECTION("Empty VarArray transposes to empty VarSoa")
  {
    Tappy<SrcSchemaType> input_instance{};
    // VarArray is empty by default

    Tappy<DestSchemaType> cpp_instance{};
    std::memset(&cpp_instance, 0, sizeof(cpp_instance));

    cpp_upgrader->upgrade(
      std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&cpp_instance, 1U}));

    const auto& dest_soa = cpp_instance.get_array_field();
    REQUIRE(dest_soa.size() == 0);
  }

  SECTION("VarArray with 3 elements transposes to VarSoa")
  {
    Tappy<SrcSchemaType> input_instance{};
    auto& src_array = input_instance.get_underlying_array_field();

    // Add 3 elements
    for (size_t i = 0; i < 3; ++i)
    {
      auto& elem = src_array.emplace_back();
      elem.set_id(static_cast<int32_t>(200 + i));
      elem.set_x(static_cast<int32_t>((i + 1) * 20));
      elem.set_y(static_cast<float>(static_cast<double>(i + 1) * 2.5));
      elem.set_z(static_cast<float>(static_cast<double>(i + 1) * 200.0));

      elem.get_mutable_nested().set_removed_field(static_cast<int8_t>(i + 5));
      elem.get_mutable_nested().set_upgraded_field(static_cast<uint32_t>(4000 + i));
    }

    Tappy<DestSchemaType> cpp_instance{};
    std::memset(&cpp_instance, 0, sizeof(cpp_instance));

    cpp_upgrader->upgrade(
      std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&cpp_instance, 1U}));

    const auto& dest_soa = cpp_instance.get_array_field();
    REQUIRE(dest_soa.size() == 3);

    for (size_t i = 0; i < 3; ++i)
    {
      CAPTURE(i);

      REQUIRE(dest_soa.view_id()[i] == static_cast<int32_t>(200 + i));
      REQUIRE(dest_soa.view_x()[i] == static_cast<int64_t>((i + 1) * 20));
      REQUIRE(dest_soa.view_y()[i] == Catch::Approx(static_cast<double>(i + 1) * 2.5));
      REQUIRE(dest_soa.view_w()[i] == 0.0f);

      const auto& nested = dest_soa.view_nested()[i];
      REQUIRE(nested.get_upgraded_field() == static_cast<uint64_t>(4000 + i));
    }
  }
}

TEST_CASE("FixedSoa to FixedArray un-transposition")
{
  // Test upgrading FixedSoa<Schema> to FixedArray<Schema>
  using SrcSchemaType = tests::FixedSoaToFixedArrayTestSchemaV1;
  using DestSchemaType = tests::FixedSoaToFixedArrayTestSchemaV2;

  const auto cpp_upgrader = make_cpp_upgrader<Tappy<SrcSchemaType>, Tappy<DestSchemaType>>();
  REQUIRE(cpp_upgrader->upgrade_required());

  // Create V1 instance with FixedSoa of 3 elements
  Tappy<SrcSchemaType> input_instance{};
  auto& src_soa = input_instance.get_mutable_soa_field();

  // Initialize 3 elements in the FixedSoa
  for (size_t i = 0; i < 3; ++i)
  {
    src_soa.view_id()[i] = static_cast<int32_t>(300 + i);
    src_soa.view_x()[i] = static_cast<int32_t>((i + 1) * 30);
    src_soa.view_y()[i] = static_cast<float>(static_cast<double>(i + 1) * 3.5);
    src_soa.view_z()[i] = static_cast<float>(static_cast<double>(i + 1) * 300.0);

    src_soa.view_nested()[i].set_removed_field(static_cast<int8_t>(i + 10));
    src_soa.view_nested()[i].set_upgraded_field(static_cast<uint32_t>(6000 + i));
  }

  // Upgrade to V2 (FixedSoa -> FixedArray)
  Tappy<DestSchemaType> cpp_instance{};
  std::memset(&cpp_instance, 0, sizeof(cpp_instance));

  cpp_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&cpp_instance, 1U}));

  // Verify un-transposed to FixedArray
  const auto dest_array = cpp_instance.get_soa_field();

  for (size_t i = 0; i < 3; ++i)
  {
    CAPTURE(i);

    REQUIRE(dest_array[i].get_id() == static_cast<int32_t>(300 + i));
    REQUIRE(dest_array[i].get_x() == static_cast<int64_t>((i + 1) * 30));
    REQUIRE(dest_array[i].get_y() == Catch::Approx(static_cast<double>(i + 1) * 3.5));
    REQUIRE(dest_array[i].get_w() == 0.0f);

    const auto& nested = dest_array[i].get_nested();
    REQUIRE(nested.get_upgraded_field() == static_cast<uint64_t>(6000 + i));
  }
}

TEST_CASE("VarSoa to VarArray un-transposition")
{
  // Test upgrading VarSoa<Schema> to VarArray<Schema>
  using SrcSchemaType = tests::VarSoaToVarArrayTestSchemaV1;
  using DestSchemaType = tests::VarSoaToVarArrayTestSchemaV2;

  const auto cpp_upgrader = make_cpp_upgrader<Tappy<SrcSchemaType>, Tappy<DestSchemaType>>();
  REQUIRE(cpp_upgrader->upgrade_required());

  SECTION("Empty VarSoa un-transposes to empty VarArray")
  {
    Tappy<SrcSchemaType> input_instance{};
    auto& src_soa = input_instance.get_mutable_soa_field();
    REQUIRE(jewels::ok(src_soa.try_resize(0)));

    Tappy<DestSchemaType> cpp_instance{};
    std::memset(&cpp_instance, 0, sizeof(cpp_instance));

    cpp_upgrader->upgrade(
      std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&cpp_instance, 1U}));

    const auto& dest_array = cpp_instance.get_soa_field();
    REQUIRE(dest_array.size() == 0);
  }

  SECTION("VarSoa with 2 elements un-transposes to VarArray")
  {
    Tappy<SrcSchemaType> input_instance{};
    auto& src_soa = input_instance.get_mutable_soa_field();
    REQUIRE(jewels::ok(src_soa.try_resize(2)));

    // Initialize 2 elements
    for (size_t i = 0; i < 2; ++i)
    {
      src_soa.view_id()[i] = static_cast<int32_t>(400 + i);
      src_soa.view_x()[i] = static_cast<int32_t>((i + 1) * 40);
      src_soa.view_y()[i] = static_cast<float>(static_cast<double>(i + 1) * 4.5);
      src_soa.view_z()[i] = static_cast<float>(static_cast<double>(i + 1) * 400.0);

      src_soa.view_nested()[i].set_removed_field(static_cast<int8_t>(i + 15));
      src_soa.view_nested()[i].set_upgraded_field(static_cast<uint32_t>(8000 + i));
    }

    Tappy<DestSchemaType> cpp_instance{};
    std::memset(&cpp_instance, 0, sizeof(cpp_instance));

    cpp_upgrader->upgrade(
      std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&cpp_instance, 1U}));

    const auto& dest_array = cpp_instance.get_soa_field();
    REQUIRE(dest_array.size() == 2);

    for (size_t i = 0; i < 2; ++i)
    {
      CAPTURE(i);

      REQUIRE(dest_array[i].get_id() == static_cast<int32_t>(400 + i));
      REQUIRE(dest_array[i].get_x() == static_cast<int64_t>((i + 1) * 40));
      REQUIRE(dest_array[i].get_y() == Catch::Approx(static_cast<double>(i + 1) * 4.5));
      REQUIRE(dest_array[i].get_w() == 0.0f);

      const auto& nested = dest_array[i].get_nested();
      REQUIRE(nested.get_upgraded_field() == static_cast<uint64_t>(8000 + i));
    }
  }
}

TEST_CASE("Optional to FixedSoa transposition")
{
  // Test upgrading Optional<Schema> to FixedSoa<Schema, size=1>
  using SrcSchemaType = tests::OptionalToFixedSoaTestSchemaV1;
  using DestSchemaType = tests::OptionalToFixedSoaTestSchemaV2;

  const auto cpp_upgrader = make_cpp_upgrader<Tappy<SrcSchemaType>, Tappy<DestSchemaType>>();
  REQUIRE(cpp_upgrader->upgrade_required());

  SECTION("Optional with value transposes to FixedSoa with 1 element")
  {
    Tappy<SrcSchemaType> input_instance{};
    auto& src_optional = input_instance.get_underlying_optional_field();
    src_optional.emplace();
    src_optional->set_id(123);
    src_optional->set_x(456);
    src_optional->set_y(7.89f);
    src_optional->set_z(101.1f);
    src_optional->get_mutable_nested().set_removed_field(1);
    src_optional->get_mutable_nested().set_upgraded_field(2000);

    Tappy<DestSchemaType> cpp_instance{};
    std::memset(&cpp_instance, 0, sizeof(cpp_instance));

    cpp_upgrader->upgrade(
      std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&cpp_instance, 1U}));

    const auto& dest_soa = cpp_instance.get_optional_field();
    // FixedSoa always has size=1, check the element values
    REQUIRE(dest_soa.view_id()[0] == 123);
    REQUIRE(dest_soa.view_x()[0] == 456);                 // Upgraded to int64
    REQUIRE(dest_soa.view_y()[0] == Catch::Approx(7.89)); // Upgraded to double
    REQUIRE(dest_soa.view_w()[0] == 0.0f);                // New field with default
    REQUIRE(dest_soa.view_nested()[0].get_upgraded_field() == 2000);
  }

  SECTION("Empty Optional transposes to FixedSoa with zeroed element")
  {
    Tappy<SrcSchemaType> input_instance{};
    // Optional is empty by default

    Tappy<DestSchemaType> cpp_instance{};
    std::memset(&cpp_instance, 0, sizeof(cpp_instance));

    cpp_upgrader->upgrade(
      std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&cpp_instance, 1U}));

    // FixedSoa still has 1 element, but values should be zeroed/default
    const auto& dest_soa = cpp_instance.get_optional_field();
    REQUIRE(dest_soa.view_id()[0] == 0);
    REQUIRE(dest_soa.view_x()[0] == 0);
  }
}

TEST_CASE("Optional to VarSoa transposition")
{
  // Test upgrading Optional<Schema> to VarSoa<Schema, max_size=1>
  using SrcSchemaType = tests::OptionalToVarSoaTestSchemaV1;
  using DestSchemaType = tests::OptionalToVarSoaTestSchemaV2;

  const auto cpp_upgrader = make_cpp_upgrader<Tappy<SrcSchemaType>, Tappy<DestSchemaType>>();
  REQUIRE(cpp_upgrader->upgrade_required());

  SECTION("Optional with value transposes to VarSoa with 1 element")
  {
    Tappy<SrcSchemaType> input_instance{};
    auto& src_optional = input_instance.get_underlying_optional_field();
    src_optional.emplace();
    src_optional->set_id(789);
    src_optional->set_x(321);
    src_optional->set_y(6.54f);
    src_optional->set_z(32.1f);
    src_optional->get_mutable_nested().set_removed_field(2);
    src_optional->get_mutable_nested().set_upgraded_field(3000);

    Tappy<DestSchemaType> cpp_instance{};
    std::memset(&cpp_instance, 0, sizeof(cpp_instance));

    cpp_upgrader->upgrade(
      std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&cpp_instance, 1U}));

    const auto& dest_soa = cpp_instance.get_optional_field();
    REQUIRE(dest_soa.size() == 1);
    REQUIRE(dest_soa.view_id()[0] == 789);
    REQUIRE(dest_soa.view_x()[0] == 321);
    REQUIRE(dest_soa.view_y()[0] == Catch::Approx(6.54));
    REQUIRE(dest_soa.view_w()[0] == 0.0f);
    REQUIRE(dest_soa.view_nested()[0].get_upgraded_field() == 3000);
  }

  SECTION("Empty Optional transposes to empty VarSoa")
  {
    Tappy<SrcSchemaType> input_instance{};
    // Optional is empty by default

    Tappy<DestSchemaType> cpp_instance{};
    std::memset(&cpp_instance, 0, sizeof(cpp_instance));

    cpp_upgrader->upgrade(
      std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&cpp_instance, 1U}));

    const auto& dest_soa = cpp_instance.get_optional_field();
    REQUIRE(dest_soa.size() == 0);
  }
}

TEST_CASE("FixedSoa to Optional un-transposition")
{
  // Test upgrading FixedSoa<Schema, size=1> to Optional<Schema>
  using SrcSchemaType = tests::FixedSoaToOptionalTestSchemaV1;
  using DestSchemaType = tests::FixedSoaToOptionalTestSchemaV2;

  const auto cpp_upgrader = make_cpp_upgrader<Tappy<SrcSchemaType>, Tappy<DestSchemaType>>();
  REQUIRE(cpp_upgrader->upgrade_required());

  // Create V1 instance with FixedSoa of 1 element
  Tappy<SrcSchemaType> input_instance{};
  auto& src_soa = input_instance.get_mutable_soa_field();

  // Initialize the single element
  src_soa.view_id()[0] = 555;
  src_soa.view_x()[0] = 666;
  src_soa.view_y()[0] = 7.77f;
  src_soa.view_z()[0] = 88.8f;
  src_soa.view_nested()[0].set_removed_field(3);
  src_soa.view_nested()[0].set_upgraded_field(4000);

  Tappy<DestSchemaType> cpp_instance{};
  std::memset(&cpp_instance, 0, sizeof(cpp_instance));

  cpp_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&cpp_instance, 1U}));

  // Verify un-transposed to Optional with value
  const auto& dest_optional = cpp_instance.get_underlying_soa_field();
  REQUIRE(dest_optional.has_value());
  REQUIRE(dest_optional->get_id() == 555);
  REQUIRE(dest_optional->get_x() == 666);
  REQUIRE(dest_optional->get_y() == Catch::Approx(7.77));
  REQUIRE(dest_optional->get_w() == 0.0f);
  REQUIRE(dest_optional->get_nested().get_upgraded_field() == 4000);
}

TEST_CASE("VarSoa to Optional un-transposition")
{
  // Test upgrading VarSoa<Schema, max_size=1> to Optional<Schema>
  using SrcSchemaType = tests::VarSoaToOptionalTestSchemaV1;
  using DestSchemaType = tests::VarSoaToOptionalTestSchemaV2;

  const auto cpp_upgrader = make_cpp_upgrader<Tappy<SrcSchemaType>, Tappy<DestSchemaType>>();
  REQUIRE(cpp_upgrader->upgrade_required());

  SECTION("VarSoa with 1 element un-transposes to Optional with value")
  {
    Tappy<SrcSchemaType> input_instance{};
    auto& src_soa = input_instance.get_mutable_soa_field();
    REQUIRE(jewels::ok(src_soa.try_resize(1)));

    src_soa.view_id()[0] = 999;
    src_soa.view_x()[0] = 888;
    src_soa.view_y()[0] = 7.77f;
    src_soa.view_z()[0] = 66.6f;
    src_soa.view_nested()[0].set_removed_field(4);
    src_soa.view_nested()[0].set_upgraded_field(5000);

    Tappy<DestSchemaType> cpp_instance{};
    std::memset(&cpp_instance, 0, sizeof(cpp_instance));

    cpp_upgrader->upgrade(
      std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&cpp_instance, 1U}));

    const auto& dest_optional = cpp_instance.get_underlying_soa_field();
    REQUIRE(dest_optional.has_value());
    REQUIRE(dest_optional->get_id() == 999);
    REQUIRE(dest_optional->get_x() == 888);
    REQUIRE(dest_optional->get_y() == Catch::Approx(7.77));
    REQUIRE(dest_optional->get_w() == 0.0f);
    REQUIRE(dest_optional->get_nested().get_upgraded_field() == 5000);
  }

  SECTION("Empty VarSoa un-transposes to empty Optional")
  {
    Tappy<SrcSchemaType> input_instance{};
    auto& src_soa = input_instance.get_mutable_soa_field();
    REQUIRE(jewels::ok(src_soa.try_resize(0)));

    Tappy<DestSchemaType> cpp_instance{};
    std::memset(&cpp_instance, 0, sizeof(cpp_instance));

    cpp_upgrader->upgrade(
      std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&cpp_instance, 1U}));

    const auto& dest_optional = cpp_instance.get_underlying_soa_field();
    REQUIRE_FALSE(dest_optional.has_value());
  }
}

TEST_CASE("UUID to VarString upgrade")
{
  // Initialize python for the log schema upgrader
  python::python_init(python::InitializationMode::unit_test);

  const auto python_upgrader = make_python_upgrader<
    Tappy<tests::UuidToVarStringUpgradeTestSchemaV1>,
    Tappy<tests::UuidToVarStringUpgradeTestSchemaV2>>();
  REQUIRE(python_upgrader->upgrade_required());

  const auto cpp_upgrader = make_cpp_upgrader<
    Tappy<tests::UuidToVarStringUpgradeTestSchemaV1>,
    Tappy<tests::UuidToVarStringUpgradeTestSchemaV2>>();
  REQUIRE(cpp_upgrader->upgrade_required());

  validate_upgradability<
    Tappy<tests::UuidToVarStringUpgradeTestSchemaV1>,
    Tappy<tests::UuidToVarStringUpgradeTestSchemaV2>>();

  Tappy<tests::UuidToVarStringUpgradeTestSchemaV1> input_instance{};
  const auto test_uuid = SchemaUuid::random_uuid();
  input_instance.set_uuid_field(test_uuid);
  input_instance.set_int32_field(42);
  Tappy<tests::UuidToVarStringUpgradeTestSchemaV2> python_instance{};
  Tappy<tests::UuidToVarStringUpgradeTestSchemaV2> cpp_instance{};
  std::memset(&python_instance, 0, sizeof(python_instance));
  std::memset(&cpp_instance, 0, sizeof(cpp_instance));

  python_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&python_instance, 1U}));
  cpp_upgrader->upgrade(
    std::as_bytes(std::span{&input_instance, 1U}), std::as_writable_bytes(std::span{&cpp_instance, 1U}));

  const auto check_python = GENERATE(true, false);
  CAPTURE(check_python);
  const auto checked_instance = check_python ? python_instance : cpp_instance;

  REQUIRE(std::string_view{checked_instance.get_uuid_field()} == test_uuid.to_string());
  REQUIRE(checked_instance.get_int32_field() == 42);

  REQUIRE(
    std::ranges::equal(std::as_bytes(std::span{&cpp_instance, 1U}), std::as_bytes(std::span{&python_instance, 1U})));
}

TEST_CASE("FixedArray to Tensor")
{
  const auto upgrader = make_cpp_upgrader<Tappy<tests::FixedArrayToTensorV1>, Tappy<tests::FixedArrayToTensorV2>>();
  // While these schemas are technivally wire-compatible, their hashes don't
  // match because FixedArray and Tensor are instantiated differently in the
  // DSL. The underlying transformation should be a trivial memcpy, though.
  REQUIRE(upgrader->upgrade_required());

  validate_upgradability<Tappy<tests::FixedArrayToTensorV1>, Tappy<tests::FixedArrayToTensorV2>>();

  Tappy<tests::FixedArrayToTensorV1> instance_v1{};
  for (size_t i = 0; i < instance_v1.get_mutable_tensor().size(); i++)
  {
    instance_v1.get_mutable_tensor()[i] = static_cast<float>(i);
  }

  Tappy<tests::FixedArrayToTensorV2> instance_v2{};
  std::memset(&instance_v2, 0, sizeof(instance_v2));
  upgrader->upgrade(std::as_bytes(std::span{&instance_v1, 1U}), std::as_writable_bytes(std::span{&instance_v2, 1U}));

  REQUIRE(std::ranges::equal(std::as_bytes(std::span{&instance_v1, 1U}), std::as_bytes(std::span{&instance_v2, 1U})));
}

TEST_CASE("FixedArray to Tensor - Mismatched Type")
{
  validate_no_upgradability<Tappy<tests::FixedArrayToTensorV1>, Tappy<tests::BadTypeFixedArrayToTensorV2>>();
}

TEST_CASE("FixedArray to Tensor - Mismatched Shape")
{
  validate_no_upgradability<Tappy<tests::FixedArrayToTensorV1>, Tappy<tests::BadShapeFixedArrayToTensorV2>>();
}

TEST_CASE("Tensor Element Type Change")
{
  const auto upgrader = make_cpp_upgrader<Tappy<tests::TensorElementChangeV1>, Tappy<tests::TensorElementChangeV2>>();
  REQUIRE(upgrader->upgrade_required());

  validate_upgradability<Tappy<tests::TensorElementChangeV1>, Tappy<tests::TensorElementChangeV2>>();

  Tappy<tests::TensorElementChangeV1> instance_v1{};
  for (size_t i = 0; i < instance_v1.get_mutable_tensor().storage().size(); i++)
  {
    instance_v1.get_mutable_tensor().storage()[i] = static_cast<int32_t>(i);
  }

  Tappy<tests::TensorElementChangeV2> instance_v2{};
  std::memset(&instance_v2, 0, sizeof(instance_v2));
  upgrader->upgrade(std::as_bytes(std::span{&instance_v1, 1U}), std::as_writable_bytes(std::span{&instance_v2, 1U}));

  REQUIRE_FALSE(
    std::ranges::equal(std::as_bytes(std::span{&instance_v1, 1U}), std::as_bytes(std::span{&instance_v2, 1U})));
  for (size_t i = 0; i < instance_v2.get_mutable_tensor().storage().size(); i++)
  {
    REQUIRE(instance_v2.get_tensor().storage()[i] == static_cast<float>(i));
  }
}

TEST_CASE("Unexpected Tensor Shape Change")
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const auto model1 = TachyonModel::from_type<Tappy<tests::UnexpectedTensorShapeChangeV1>>(memory_resource);
  const auto model2 = TachyonModel::from_type<Tappy<tests::UnexpectedTensorShapeChangeV2>>(memory_resource);
  REQUIRE_THROWS(model2->check_for_unexpected_schema_changes(*model1));
  validate_no_upgradability<Tappy<tests::UnexpectedTensorShapeChangeV1>, Tappy<tests::UnexpectedTensorShapeChangeV2>>();
}

TEST_CASE("Unexpected Tensor Element Change")
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const auto model1 = TachyonModel::from_type<Tappy<tests::UnexpectedTensorElementChangeV1>>(memory_resource);
  const auto model2 = TachyonModel::from_type<Tappy<tests::UnexpectedTensorElementChangeV2>>(memory_resource);
  REQUIRE_THROWS(model2->check_for_unexpected_schema_changes(*model1));
  validate_no_upgradability<
    Tappy<tests::UnexpectedTensorElementChangeV1>,
    Tappy<tests::UnexpectedTensorElementChangeV2>>();
}

} // namespace
} // namespace clockwork::serialization
