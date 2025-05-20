// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/memory/bits.hh"
#include "jewels/memory/fields.hh"
#include "jewels/meta/concepts.hh"
#include "jewels/std/span.hh"

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <endian.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace jewels::memory
{

struct TestType
{
  double value_dbl;
  float value_flt;
  uint32_t value_32;
  uint16_t value_16;
  uint8_t value_8;
};

struct Constants
{
  static constexpr auto double_offset{0U};
  static constexpr auto double_bytes{8U};

  static constexpr auto float_offset{double_offset + double_bytes};
  static constexpr auto float_bytes{4U};

  static constexpr auto uint32_offset{float_offset + float_bytes};
  static constexpr auto uint32_bytes{4U};

  static constexpr auto uint16_offset{uint32_offset + uint32_bytes};
  static constexpr auto uint16_bytes{2U};

  static constexpr auto uint8_offset{uint16_offset + uint16_bytes};
  static constexpr auto uint8_bytes{1U};

  static constexpr auto total_bytes{double_bytes + float_bytes + uint32_bytes + uint16_bytes + uint8_bytes};
};

using DoubleField = Field<double, Constants::double_offset, Constants::double_bytes>;
using FloatField = Field<float, Constants::float_offset, Constants::float_bytes>;
using UInt32Field = Field<uint32_t, Constants::uint32_offset, Constants::uint32_bytes>;
using UInt16Field = Field<uint16_t, Constants::uint16_offset, Constants::uint16_bytes>;
using UInt8Field = Field<uint8_t, Constants::uint8_offset, Constants::uint8_bytes>;

TEST_CASE("Round trip")
{
  std::array<uint8_t, Constants::total_bytes> buffer{};
  TestType data_in{};
  TestType data_out{};

  data_in.value_dbl = 9.81;
  data_in.value_flt = 3.14f;
  data_in.value_32 = 4'000'000'000U;
  data_in.value_16 = 65432U;
  data_in.value_8 = 210U;

  write_field<DoubleField>(data_in.value_dbl, as_writable_bytes(std::span<uint8_t>(buffer)));
  write_field<FloatField>(data_in.value_flt, as_writable_bytes(std::span<uint8_t>(buffer)));
  write_field<UInt32Field>(data_in.value_32, as_writable_bytes(std::span<uint8_t>(buffer)));
  write_field<UInt16Field>(data_in.value_16, as_writable_bytes(std::span<uint8_t>(buffer)));
  write_field<UInt8Field>(data_in.value_8, as_writable_bytes(std::span<uint8_t>(buffer)));

  data_out.value_dbl = read_field<DoubleField>(as_bytes(std::span<uint8_t>(buffer)));
  data_out.value_flt = read_field<FloatField>(as_bytes(std::span<uint8_t>(buffer)));
  data_out.value_32 = read_field<UInt32Field>(as_bytes(std::span<uint8_t>(buffer)));
  data_out.value_16 = read_field<UInt16Field>(as_bytes(std::span<uint8_t>(buffer)));
  data_out.value_8 = read_field<UInt8Field>(as_bytes(std::span<uint8_t>(buffer)));

  REQUIRE(data_in.value_dbl == data_out.value_dbl);
  REQUIRE(data_in.value_flt == data_out.value_flt);
  REQUIRE(data_in.value_32 == data_out.value_32);
  REQUIRE(data_in.value_16 == data_out.value_16);
  REQUIRE(data_in.value_8 == data_out.value_8);
}

TEMPLATE_TEST_CASE("Endianness transformers", "", uint16_t, uint32_t, uint64_t, int16_t, int32_t, int64_t)
{
  // Just want to test dispatch. No need to test the byteswap logic provided by endian.h
  if constexpr (std::is_same_v<TestType, uint64_t> || std::is_same_v<TestType, int64_t>)
  {
    auto value = static_cast<TestType>(0xDEADBEEFFEEDBEEF);
    REQUIRE(ToBigEndian{}(value) == static_cast<TestType>(htobe64(value)));
    REQUIRE(FromBigEndian{}(value) == static_cast<TestType>(be64toh(value)));
    REQUIRE(ToLittleEndian{}(value) == static_cast<TestType>(htole64(value)));
    REQUIRE(FromLittleEndian{}(value) == static_cast<TestType>(le64toh(value)));
  }
  if constexpr (std::is_same_v<TestType, uint32_t> || std::is_same_v<TestType, int32_t>)
  {
    auto value = static_cast<TestType>(0xDEADBEEF);
    REQUIRE(ToBigEndian{}(value) == static_cast<TestType>(htobe32(value)));
    REQUIRE(FromBigEndian{}(value) == static_cast<TestType>(be32toh(value)));
    REQUIRE(ToLittleEndian{}(value) == static_cast<TestType>(htole32(value)));
    REQUIRE(FromLittleEndian{}(value) == static_cast<TestType>(le32toh(value)));
  }
  if constexpr (std::is_same_v<TestType, uint16_t> || std::is_same_v<TestType, int16_t>)
  {
    auto value = static_cast<TestType>(0xBEEF);
    REQUIRE(ToBigEndian{}(value) == static_cast<TestType>(htobe16(value)));
    REQUIRE(FromBigEndian{}(value) == static_cast<TestType>(be16toh(value)));
    REQUIRE(ToLittleEndian{}(value) == static_cast<TestType>(htole16(value)));
    REQUIRE(FromLittleEndian{}(value) == static_cast<TestType>(le16toh(value)));
  }
}

TEMPLATE_TEST_CASE("Round-trip endianness transform", "", uint16_t, uint32_t, uint64_t, int16_t, int32_t, int64_t)
{
  static_assert(sizeof(TestType) > 1);
  std::array<uint8_t, sizeof(TestType)> buffer{};
  TestType expected_value{};
  if constexpr (std::is_same_v<TestType, uint64_t> || std::is_same_v<TestType, int64_t>)
  {
    expected_value = static_cast<TestType>(0xDEADBEEFFEEDBEEF);
  }
  if constexpr (std::is_same_v<TestType, uint32_t> || std::is_same_v<TestType, int32_t>)
  {
    expected_value = static_cast<TestType>(0xDEADBEEF);
  }
  if constexpr (std::is_same_v<TestType, uint16_t> || std::is_same_v<TestType, int16_t>)
  {
    expected_value = static_cast<TestType>(0xBEEF);
  }

  SECTION("Big Endian")
  {
    using FieldBig = Field<TestType, size_t{0}, sizeof(TestType), FromBigEndian{}, ToBigEndian{}>;
    write_field<FieldBig>(expected_value, as_writable_bytes(std::span(buffer)));
    REQUIRE(read_field<FieldBig>(as_bytes(std::span(buffer))) == expected_value);
  }

  SECTION("Little Endian")
  {
    using FieldLittle = Field<TestType, size_t{0}, sizeof(TestType), FromLittleEndian{}, ToLittleEndian{}>;
    write_field<FieldLittle>(expected_value, as_writable_bytes(std::span(buffer)));
    REQUIRE(read_field<FieldLittle>(as_bytes(std::span(buffer))) == expected_value);
  }
}

struct Void
{
};

TEST_CASE("Transform changes type")
{
  SECTION("Concept")
  {
    STATIC_REQUIRE(
      ValidFieldTransform<uint8_t, decltype([](auto) { return bool{}; }), decltype([](auto) { return uint8_t{}; })>);
    // Read doesn't accept underlying value.
    STATIC_REQUIRE(
      !ValidFieldTransform<uint8_t, decltype([](Void) { return bool{}; }), decltype([](auto) { return uint8_t{}; })>);
    // Write doesn't accept output of read.
    STATIC_REQUIRE(
      !ValidFieldTransform<uint8_t, decltype([](auto) { return bool{}; }), decltype([](Void) { return uint8_t{}; })>);
    // Write doesn't produce a value taht decays to the underlying type.
    STATIC_REQUIRE(
      !ValidFieldTransform<uint8_t, decltype([](auto) { return bool{}; }), decltype([](auto) { return Void{}; })>);
  }
  SECTION("Round trip")
  {
    constexpr auto read_transform = [](uint8_t value) { return static_cast<bool>(value); };
    constexpr auto write_transform = [](bool value) { return static_cast<uint8_t>(value); };
    using TestField = Field<uint8_t, size_t{0}, sizeof(uint8_t), read_transform, write_transform>;
    const uint8_t original{1U};
    const auto read_value = read_field<TestField>(as_bytes(jewels::as_single_item_span(original)));
    STATIC_REQUIRE(jewels::meta::DecaysTo<decltype(read_value), bool>);
    REQUIRE(read_value);
    uint8_t written_value{}; // NOLINT(misc-const-correctness) false positive.
    REQUIRE(written_value != original);
    write_field<TestField>(read_value, as_writable_bytes(jewels::as_single_item_span(written_value)));
    REQUIRE(written_value == original);
  }
}

} // namespace jewels::memory
