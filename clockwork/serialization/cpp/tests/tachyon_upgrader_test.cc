// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/python/python_init.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/serialization/cpp/clk_builtin_type.hh"
#include "clockwork/serialization/cpp/clk_type.hh"
#include "clockwork/serialization/cpp/tachyon_python_upgrader.hh"
#include "clockwork/serialization/cpp/tachyon_upgrader.hh"
#include "clockwork/serialization/cpp/tests/support/test_schema_v1.hh"
#include "clockwork/serialization/cpp/tests/support/test_schema_v2.hh"
#include "clockwork/serialization/cpp/tests/support/validate_upgradability.hh"
#include "clockwork/serialization/metadata/tachyon_model.pb.h"
#include "clockwork/serialization/py/tests/support/simple_schema_v1.hh"
#include "clockwork/serialization/py/tests/support/simple_schema_v2.hh"
#include "jewels/container/compare.hh"
#include "jewels/container/tap/optional.hh"
#include "jewels/container/tap/var_array.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

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
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <typeinfo>
#include <utility>

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

} // namespace
} // namespace clockwork::serialization
