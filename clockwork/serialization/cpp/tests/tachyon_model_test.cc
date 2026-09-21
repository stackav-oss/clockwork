// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/repr_iface.hh"
#include "clockwork/serialization/cpp/clk_type.hh"
#include "clockwork/serialization/cpp/metadata_versions.hh"
#include "clockwork/serialization/cpp/tachyon_model.hh"
#include "clockwork/serialization/cpp/tests/support/test_schema_v2_clk_cc.hh"
#include "clockwork/serialization/cpp/tests/support/validate_upgradability.hh"
#include "clockwork/serialization/metadata/tachyon_model.pb.h"
#include "clockwork/serialization/py/tests/support/broken_schema_v1_clk_cc.hh"
#include "clockwork/serialization/py/tests/support/broken_schema_v2_clk_cc.hh"
#include "jewels/container/tap/bitset.hh"
#include "jewels/memory/memory_resource.hh"

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>

namespace clockwork::serialization
{
namespace
{

[[nodiscard]] std::string to_hex(std::span<const std::byte> data)
{
  constexpr uint8_t hex_digit_mask = 0xF;
  constexpr uint8_t hex_digit_0_shift = 0;
  constexpr uint8_t hex_digit_1_shift = 4;
  constexpr std::string_view chars = "0123456789abcdef";
  std::string hex_str;
  hex_str.reserve(data.size() * 2U);
  for (const auto datum : data)
  {
    hex_str.push_back(chars[static_cast<uint8_t>(static_cast<uint8_t>(datum) >> hex_digit_1_shift) & hex_digit_mask]);
    hex_str.push_back(chars[static_cast<uint8_t>(static_cast<uint8_t>(datum) >> hex_digit_0_shift) & hex_digit_mask]);
  }
  return hex_str;
}

TEST_CASE("Get metadata hash and version")
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  auto metadata_proto = std::make_unique<metadata::TachyonMetadata>();
  const auto& schema_definition = LoggingTraits<Tappy<tests::SmokeTestSchemaV2>>::schema_definition;
  REQUIRE(metadata_proto->ParseFromString(std::string{schema_definition.data(), schema_definition.size()}));
  metadata_proto->set_version(99);
  const std::string test_hash{"0123456789abcdef"};
  metadata_proto->mutable_types(metadata_proto->outer_type_id())->mutable_schema()->set_hash(test_hash);

  const auto model = TachyonModel::from_proto(memory_resource, std::move(metadata_proto));
  REQUIRE(model->get_metadata_version() == 99);
  REQUIRE(std::ranges::equal(model->get_metadata_hash(), std::as_bytes(std::span{test_hash.data(), test_hash.size()})));
  REQUIRE(model->get_outer_type().get_type_id() == ClkTypeId::schema);
  REQUIRE(model->get_outer_type().get_fqn().ends_with("SmokeTestSchemaV2"));
}

TEST_CASE("Load Bitset built-in type from metadata")
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const auto model = TachyonModel::from_type<Tappy<tests::BitsetTestSchemaV2>>(memory_resource);
  REQUIRE(model->get_outer_type().get_type_id() == ClkTypeId::schema);

  Tappy<tests::BitsetTestSchemaV2> value{};
  value.get_mutable_sensor_present().set(0U).set(7U).set(9U);
  REQUIRE(sizeof(value) == 2U);
  REQUIRE(value.get_sensor_present().bytes()[0] == std::byte{0x81});
  REQUIRE(value.get_sensor_present().bytes()[1] == std::byte{0x02});
}

using TestTypes = std::tuple<
  tests::SmokeTestSchemaV2,
  tests::Int8UpgradeTestSchemaV2,
  tests::Int16UpgradeTestSchemaV2,
  tests::Int32UpgradeTestSchemaV2,
  tests::Int64UpgradeTestSchemaV2,
  tests::UInt8UpgradeTestSchemaV2,
  tests::UInt16UpgradeTestSchemaV2,
  tests::UInt32UpgradeTestSchemaV2,
  tests::UInt64UpgradeTestSchemaV2,
  tests::CopyableMemberSchemaTypeV2,
  tests::UpgradedMemberSchemaTypeV2,
  tests::SchemaUpgradeTestSchemaV2,
  tests::Float32UpgradeTestSchemaV2,
  tests::Float64UpgradeTestSchemaV2,
  tests::PrimitiveUpgradeTestSchemaV2,
  tests::SyncTimeUpgradeTestSchemaV2,
  tests::DurationUpgradeTestSchemaV2,
  tests::StrongTypeUpgradeTestSchemaV2,
  tests::FixedArrayUpgradeTestSchemaV2,
  tests::VarArrayUpgradeTestSchemaV2,
  tests::OptionalUpgradeTestSchemaV2,
  tests::VarStringUpgradeTestSchemaV2,
  tests::BuildTimeUpgradeFailureTestSchemaV2,
  tests::RunTimeUpgradeFailureTestSchemaV2,
  tests::Int8ValueEnumUpgradeTestSchemaV2,
  tests::Int16ValueEnumUpgradeTestSchemaV2,
  tests::Int32ValueEnumUpgradeTestSchemaV2,
  tests::Int64ValueEnumUpgradeTestSchemaV2,
  tests::UInt8ValueEnumUpgradeTestSchemaV2,
  tests::UInt16ValueEnumUpgradeTestSchemaV2,
  tests::UInt32ValueEnumUpgradeTestSchemaV2,
  tests::UInt64ValueEnumUpgradeTestSchemaV2,
  tests::UInt8BitFlagEnumUpgradeTestSchemaV2,
  tests::UInt16BitFlagEnumUpgradeTestSchemaV2,
  tests::UInt32BitFlagEnumUpgradeTestSchemaV2,
  tests::UInt64BitFlagEnumUpgradeTestSchemaV2>;

TEMPLATE_LIST_TEST_CASE("compute_metadata_hash", "", TestTypes)
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const auto model = TachyonModel::from_type<Tappy<TestType>>(memory_resource);
  REQUIRE(model->get_metadata_version() == latest_metadata_protobuf_version);
  CHECK(to_hex(model->get_metadata_hash()) == to_hex(model->compute_metadata_hash()));
  REQUIRE(std::ranges::equal(model->get_metadata_hash(), model->compute_metadata_hash()));
}

TEST_CASE("Broken integer field")
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const auto model1 = TachyonModel::from_type<Tappy<tests::BrokenIntV1>>(memory_resource);
  const auto model2 = TachyonModel::from_type<Tappy<tests::BrokenIntV2>>(memory_resource);
  REQUIRE_THROWS_WITH(
    model2->check_for_unexpected_schema_changes(*model1),
    Catch::Matchers::ContainsSubstring("Unexpected type change from int32 to int64"));

  validate_no_upgradability<Tappy<tests::BrokenIntV1>, Tappy<tests::BrokenIntV2>>();
}

TEST_CASE("Broken optional field")
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const auto model1 = TachyonModel::from_type<Tappy<tests::BrokenOptionalV1>>(memory_resource);
  const auto model2 = TachyonModel::from_type<Tappy<tests::BrokenOptionalV2>>(memory_resource);
  REQUIRE_THROWS_WITH(
    model2->check_for_unexpected_schema_changes(*model1),
    Catch::Matchers::ContainsSubstring("Unexpected type change from int32 to int64"));

  validate_no_upgradability<Tappy<tests::BrokenOptionalV1>, Tappy<tests::BrokenOptionalV2>>();
}

TEST_CASE("Broken array field")
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const auto model1 = TachyonModel::from_type<Tappy<tests::BrokenArrayV1>>(memory_resource);
  const auto model2 = TachyonModel::from_type<Tappy<tests::BrokenArrayV2>>(memory_resource);
  REQUIRE_THROWS_WITH(
    model2->check_for_unexpected_schema_changes(*model1),
    Catch::Matchers::ContainsSubstring("Unexpected type change from int32 to int64"));

  validate_no_upgradability<Tappy<tests::BrokenArrayV1>, Tappy<tests::BrokenArrayV2>>();
}

TEST_CASE("Broken string field")
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const auto model1 = TachyonModel::from_type<Tappy<tests::BrokenStringV1>>(memory_resource);
  const auto model2 = TachyonModel::from_type<Tappy<tests::BrokenStringV2>>(memory_resource);
  REQUIRE_THROWS_WITH(
    model2->check_for_unexpected_schema_changes(*model1),
    Catch::Matchers::ContainsSubstring("Unexpected string size change from 64 to 128"));

  validate_no_upgradability<Tappy<tests::BrokenStringV1>, Tappy<tests::BrokenStringV2>>();
}

TEST_CASE("Broken builtin field")
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const auto model1 = TachyonModel::from_type<Tappy<tests::BrokenBuiltInV1>>(memory_resource);
  const auto model2 = TachyonModel::from_type<Tappy<tests::BrokenBuiltInV2>>(memory_resource);
  REQUIRE_THROWS_WITH(
    model2->check_for_unexpected_schema_changes(*model1),
    Catch::Matchers::ContainsSubstring("Unexpected type change from fixed_array to var_array"));

  validate_no_upgradability<Tappy<tests::BrokenBuiltInV1>, Tappy<tests::BrokenBuiltInV2>>();
}

TEST_CASE("Broken name field")
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const auto model1 = TachyonModel::from_type<Tappy<tests::BrokenNameV1>>(memory_resource);
  const auto model2 = TachyonModel::from_type<Tappy<tests::BrokenNameV2>>(memory_resource);
  REQUIRE_THROWS_WITH(
    model2->check_for_unexpected_schema_changes(*model1),
    Catch::Matchers::ContainsSubstring("Field integer_field renamed to renamed_integer_field"));

  validate_no_upgradability<Tappy<tests::BrokenNameV1>, Tappy<tests::BrokenNameV2>>();
}

TEST_CASE("Broken removed field")
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const auto model1 = TachyonModel::from_type<Tappy<tests::BrokenRemovedV1>>(memory_resource);
  const auto model2 = TachyonModel::from_type<Tappy<tests::BrokenRemovedV2>>(memory_resource);
  REQUIRE_THROWS_WITH(
    model2->check_for_unexpected_schema_changes(*model1),
    Catch::Matchers::ContainsSubstring("Field 2 (integer_field2) removed"));

  validate_no_upgradability<Tappy<tests::BrokenRemovedV1>, Tappy<tests::BrokenRemovedV2>>();
}

TEST_CASE("Broken added field")
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const auto model1 = TachyonModel::from_type<Tappy<tests::BrokenAddedV1>>(memory_resource);
  const auto model2 = TachyonModel::from_type<Tappy<tests::BrokenAddedV2>>(memory_resource);
  REQUIRE_THROWS_WITH(
    model2->check_for_unexpected_schema_changes(*model1),
    Catch::Matchers::ContainsSubstring("Field 3 (integer_field3) added"));

  validate_no_upgradability<Tappy<tests::BrokenAddedV1>, Tappy<tests::BrokenAddedV2>>();
}

TEST_CASE("Broken strong type")
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const auto model1 = TachyonModel::from_type<Tappy<tests::BrokenStrongTypeV1>>(memory_resource);
  const auto model2 = TachyonModel::from_type<Tappy<tests::BrokenStrongTypeV2>>(memory_resource);
  REQUIRE_THROWS_WITH(
    model2->check_for_unexpected_schema_changes(*model1),
    Catch::Matchers::ContainsSubstring("Unexpected type change from int32 to int64"));

  validate_no_upgradability<Tappy<tests::BrokenStrongTypeV1>, Tappy<tests::BrokenStrongTypeV2>>();
}

TEST_CASE("Broken enum name")
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const auto model1 = TachyonModel::from_type<Tappy<tests::BrokenEnumNameV1>>(memory_resource);
  const auto model2 = TachyonModel::from_type<Tappy<tests::BrokenEnumNameV2>>(memory_resource);
  REQUIRE_THROWS_WITH(
    model2->check_for_unexpected_schema_changes(*model1),
    Catch::Matchers::ContainsSubstring("Value value2 renamed to value_two"));

  validate_no_upgradability<Tappy<tests::BrokenEnumNameV1>, Tappy<tests::BrokenEnumNameV2>>();
}

TEST_CASE("Broken enum value")
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const auto model1 = TachyonModel::from_type<Tappy<tests::BrokenEnumValueV1>>(memory_resource);
  const auto model2 = TachyonModel::from_type<Tappy<tests::BrokenEnumValueV2>>(memory_resource);
  REQUIRE_THROWS_WITH(
    model2->check_for_unexpected_schema_changes(*model1),
    Catch::Matchers::ContainsSubstring("Value for value2 changed from 1 to 0 "));

  validate_no_upgradability<Tappy<tests::BrokenEnumValueV1>, Tappy<tests::BrokenEnumValueV2>>();
}

TEST_CASE("Broken enum removed")
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const auto model1 = TachyonModel::from_type<Tappy<tests::BrokenEnumRemovedV1>>(memory_resource);
  const auto model2 = TachyonModel::from_type<Tappy<tests::BrokenEnumRemovedV2>>(memory_resource);
  REQUIRE_THROWS_WITH(
    model2->check_for_unexpected_schema_changes(*model1),
    Catch::Matchers::ContainsSubstring("Value 2 (value2) removed"));

  validate_no_upgradability<Tappy<tests::BrokenEnumRemovedV1>, Tappy<tests::BrokenEnumRemovedV2>>();
}

TEST_CASE("Broken enum added")
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const auto model1 = TachyonModel::from_type<Tappy<tests::BrokenEnumAddedV1>>(memory_resource);
  const auto model2 = TachyonModel::from_type<Tappy<tests::BrokenEnumAddedV2>>(memory_resource);
  REQUIRE_THROWS_WITH(
    model2->check_for_unexpected_schema_changes(*model1), Catch::Matchers::ContainsSubstring("Value 3 (value3) added"));

  validate_no_upgradability<Tappy<tests::BrokenEnumAddedV1>, Tappy<tests::BrokenEnumAddedV2>>();
}

} // namespace
} // namespace clockwork::serialization
