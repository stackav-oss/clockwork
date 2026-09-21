// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/lite_compressor.hh"
#include "clockwork/logging/onboard/types.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/serialization/cpp/tachyon_lite_compressor.hh"
#include "clockwork/serialization/cpp/tests/support/compression_test_schema_clk_cc.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/container/compare.hh"
#include "jewels/container/tap/optional.hh"
#include "jewels/container/tap/soa.hh"
#include "jewels/container/tap/tensor.hh"
#include "jewels/container/tap/var_array.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/span.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_tostring.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <memory_resource>
#include <span>
#include <string_view>
#include <vector>

namespace clockwork::serialization
{
namespace
{

using jewels::ok;
using jewels::Out;

/// Copy the compressed data spans into a vector
/// @param[in] compressed_spans Compressed data spans
/// @return Vector containing the compressed data
[[nodiscard]] std::vector<std::byte> copy_compressed_spans(std::span<const std::span<const std::byte>> compressed_spans)
{
  std::vector<std::byte> buffer(clockwork_logging::onboard::data_spans_size(compressed_spans));
  clockwork_logging::onboard::copy_data_spans(compressed_spans, buffer);
  return buffer;
}

TEST_CASE("Uncompressible types")
{
  const bool set_values = GENERATE(false, true);
  CAPTURE(set_values);
  Tappy<tests::UncompressibleTypes> instance;

  if (set_values)
  {
    instance.set_int8_field(1);
    instance.set_int16_field(2);
    instance.set_int32_field(3);
    instance.set_int64_field(4);
    instance.set_uint8_field(5);
    instance.set_uint16_field(6);
    instance.set_uint32_field(7);
    instance.set_uint64_field(8);
    instance.set_float32_field(9.0f);
    instance.set_float64_field(10.0);
    instance.set_duration_field(std::chrono::seconds(11));
    instance.set_synctime_field(jewels::time::SyncTime{std::chrono::seconds(12)});
    instance.set_uuid_field(jewels::Uuid<int8_t>::random_uuid());
  }

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const auto compressor =
    TachyonLiteCompressor::make_compressor<Tappy<tests::UncompressibleTypes>>(memory_resource, "test");
  uint64_t counts_checksum{};
  uint64_t data_checksum{};
  std::span<const std::span<const std::byte>> spans;
  compressor->compress(
    Out{spans}, Out{counts_checksum}, Out{data_checksum}, std::as_bytes(jewels::as_single_item_span(instance)));
  if (set_values)
  {
    REQUIRE(spans.size() == 2U);
    REQUIRE(spans[0].size() == 12U);
    REQUIRE(spans[1].size() == sizeof(instance));
  }
  else
  {
    REQUIRE(spans.size() == 1U);
    REQUIRE(spans[0].size() == 12U);
  }
  clockwork_logging::LiteCompressor decompressor{memory_resource};
  const auto compressed_data = copy_compressed_spans(spans);
  std::vector<std::byte> decompressed_data(sizeof(instance));
  REQUIRE(ok(decompressor.decompress(counts_checksum, data_checksum, compressed_data, decompressed_data)));
  REQUIRE(std::ranges::equal(decompressed_data, std::as_bytes(jewels::as_single_item_span(instance))));
}

TEST_CASE("FixedArray of VarArray")
{
  const auto var_array_size = GENERATE(0U, 1U, 2U, 3U);
  CAPTURE(var_array_size);

  const uint64_t element = 42U;

  Tappy<tests::FixedArrayOfVarArray> instance;
  for (auto& var_array : instance.get_mutable_fixed_var_array())
  {
    for (size_t i = 0; i < var_array_size; ++i)
    {
      var_array.emplace_back(element);
    }
  }

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const auto compressor =
    TachyonLiteCompressor::make_compressor<Tappy<tests::FixedArrayOfVarArray>>(memory_resource, "test");
  uint64_t counts_checksum{};
  uint64_t data_checksum{};
  std::span<const std::span<const std::byte>> spans;
  compressor->compress(
    Out{spans}, Out{counts_checksum}, Out{data_checksum}, std::as_bytes(jewels::as_single_item_span(instance)));

  switch (var_array_size)
  {
  case 0U:
    REQUIRE(spans.size() == 1U);
    REQUIRE(spans[0].size() == 12U);
    break;
  case 1U:
    REQUIRE(spans.size() == 4U);
    REQUIRE(spans[0].size() == 7U * sizeof(int32_t));
    REQUIRE(spans[1].size() == sizeof(element));
    REQUIRE(spans[2].size() == sizeof(element) + sizeof(uint64_t));
    REQUIRE(spans[3].size() == sizeof(uint64_t));
    break;
  case 2U:
    [[fallthrough]];
  case 3U:
    REQUIRE(spans.size() == 2U);
    REQUIRE(spans[0].size() == 12U);
    REQUIRE(spans[1].size() == sizeof(instance));
    break;
  default:
    REQUIRE(false);
  }

  clockwork_logging::LiteCompressor decompressor{memory_resource};
  const auto compressed_data = copy_compressed_spans(spans);
  std::vector<std::byte> decompressed_data(sizeof(instance));
  REQUIRE(ok(decompressor.decompress(counts_checksum, data_checksum, compressed_data, decompressed_data)));
  REQUIRE(std::ranges::equal(decompressed_data, std::as_bytes(jewels::as_single_item_span(instance))));
}

TEST_CASE("VarArray of VarArray")
{
  const auto outer_array_size = GENERATE(0U, 1U, 2U);
  CAPTURE(outer_array_size);
  const auto inner_array_size = GENERATE(0U, 1U, 2U, 3U);
  CAPTURE(inner_array_size);

  const uint64_t element = 42U;

  Tappy<tests::VarArrayOfVarArray> instance;
  for (size_t outer_index = 0; outer_index < outer_array_size; ++outer_index)
  {
    auto& inner_array = instance.get_underlying_var_var_array().emplace_back();
    for (size_t i = 0; i < inner_array_size; ++i)
    {
      inner_array.emplace_back(element);
    }
  }

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const auto compressor =
    TachyonLiteCompressor::make_compressor<Tappy<tests::VarArrayOfVarArray>>(memory_resource, "test");
  uint64_t counts_checksum{};
  uint64_t data_checksum{};
  std::span<const std::span<const std::byte>> spans;
  compressor->compress(
    Out{spans}, Out{counts_checksum}, Out{data_checksum}, std::as_bytes(jewels::as_single_item_span(instance)));

  switch (outer_array_size)
  {
  case 0U:
    REQUIRE(spans.size() == 1U);
    REQUIRE(spans[0].size() == 12U);
    break;
  case 1U:
    switch (inner_array_size)
    {
    case 0U:
      REQUIRE(spans.size() == 2U);
      REQUIRE(spans[0].size() == 16U);
      REQUIRE(spans[1].size() == sizeof(uint64_t));
      break;
    case 1U:
      REQUIRE(spans.size() == 4U);
      REQUIRE(spans[0].size() == 7U * sizeof(int32_t));
      REQUIRE(spans[1].size() == sizeof(element));
      REQUIRE(spans[2].size() == sizeof(uint64_t));
      REQUIRE(spans[3].size() == sizeof(uint64_t));
      break;
    case 2U:
      [[fallthrough]];
    case 3U:
      REQUIRE(spans.size() == 3U);
      REQUIRE(spans[0].size() == 20U);
      REQUIRE(spans[1].size() == (3U * sizeof(element)) + sizeof(uint64_t));
      REQUIRE(spans[2].size() == sizeof(uint64_t));
      break;
    default:
      REQUIRE(false);
    }
    break;
  case 2U:
    switch (inner_array_size)
    {
    case 0U:
      REQUIRE(spans.size() == 2U);
      REQUIRE(spans[0].size() == 16U);
      REQUIRE(spans[1].size() == sizeof(uint64_t));
      break;
    case 1U:
      REQUIRE(spans.size() == 4U);
      REQUIRE(spans[0].size() == 7U * sizeof(int32_t));
      REQUIRE(spans[1].size() == sizeof(element));
      REQUIRE(spans[2].size() == sizeof(element) + sizeof(uint64_t));
      REQUIRE(spans[3].size() == 2U * sizeof(uint64_t));
      break;
    case 2U:
      [[fallthrough]];
    case 3U:
      REQUIRE(spans.size() == 2U);
      REQUIRE(spans[0].size() == 12U);
      REQUIRE(spans[1].size() == sizeof(instance));
      break;
    default:
      REQUIRE(false);
    }
    break;
  default:
    REQUIRE(false);
  }

  clockwork_logging::LiteCompressor decompressor{memory_resource};
  const auto compressed_data = copy_compressed_spans(spans);
  std::vector<std::byte> decompressed_data(sizeof(instance));
  REQUIRE(ok(decompressor.decompress(counts_checksum, data_checksum, compressed_data, decompressed_data)));
  REQUIRE(std::ranges::equal(decompressed_data, std::as_bytes(jewels::as_single_item_span(instance))));
}

TEST_CASE("Two VarArray of VarArray")
{
  const auto outer_array_size = GENERATE(0U, 1U, 2U);
  CAPTURE(outer_array_size);
  const auto inner_array_size = GENERATE(0U, 1U, 2U, 3U);
  CAPTURE(inner_array_size);

  const uint64_t element1 = 27U;
  const uint64_t element2 = 42U;

  Tappy<tests::TwoVarArrayOfVarArray> instance;
  for (size_t outer_index = 0; outer_index < outer_array_size; ++outer_index)
  {
    auto& inner_array1 = instance.get_underlying_var_var_array1().emplace_back();
    auto& inner_array2 = instance.get_underlying_var_var_array2().emplace_back();
    for (size_t i = 0; i < inner_array_size; ++i)
    {
      inner_array1.emplace_back(element1);
      inner_array2.emplace_back(element2);
    }
  }

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const auto compressor =
    TachyonLiteCompressor::make_compressor<Tappy<tests::TwoVarArrayOfVarArray>>(memory_resource, "test");
  uint64_t counts_checksum{};
  uint64_t data_checksum{};
  std::span<const std::span<const std::byte>> spans;
  compressor->compress(
    Out{spans}, Out{counts_checksum}, Out{data_checksum}, std::as_bytes(jewels::as_single_item_span(instance)));

  switch (outer_array_size)
  {
  case 0U:
    REQUIRE(spans.size() == 1U);
    REQUIRE(spans[0].size() == 12U);
    break;
  case 1U:
    switch (inner_array_size)
    {
    case 0U:
      REQUIRE(spans.size() == 3U);
      REQUIRE(spans[0].size() == 24U);
      REQUIRE(spans[1].size() == sizeof(uint64_t));
      REQUIRE(spans[2].size() == sizeof(uint64_t));
      break;
    case 1U:
      REQUIRE(spans.size() == 6U);
      REQUIRE(spans[0].size() == 11U * sizeof(int32_t));
      REQUIRE(spans[1].size() == sizeof(uint64_t));
      REQUIRE(spans[2].size() == sizeof(uint64_t));
      REQUIRE(spans[3].size() == 2U * sizeof(uint64_t));
      REQUIRE(spans[4].size() == sizeof(uint64_t));
      REQUIRE(spans[5].size() == sizeof(uint64_t));
      break;
    case 2U:
      [[fallthrough]];
    case 3U:
      REQUIRE(spans.size() == 4U);
      REQUIRE(spans[0].size() == 28U);
      REQUIRE(spans[1].size() == 4U * sizeof(uint64_t));
      REQUIRE(spans[2].size() == 5U * sizeof(uint64_t));
      REQUIRE(spans[3].size() == sizeof(uint64_t));
      break;
    default:
      REQUIRE(false);
    }
    break;
  case 2U:
    switch (inner_array_size)
    {
    case 0U:
      REQUIRE(spans.size() == 3U);
      REQUIRE(spans[0].size() == 24U);
      REQUIRE(spans[1].size() == sizeof(uint64_t));
      REQUIRE(spans[2].size() == sizeof(uint64_t));
      break;
    case 1U:
      REQUIRE(spans.size() == 6U);
      REQUIRE(spans[0].size() == 11U * sizeof(int32_t));
      REQUIRE(spans[1].size() == sizeof(uint64_t));
      REQUIRE(spans[2].size() == 2U * sizeof(uint64_t));
      REQUIRE(spans[3].size() == 3U * sizeof(uint64_t));
      REQUIRE(spans[4].size() == 2U * sizeof(uint64_t));
      REQUIRE(spans[5].size() == 2U * sizeof(uint64_t));
      break;
    case 2U:
      [[fallthrough]];
    case 3U:
      REQUIRE(spans.size() == 2U);
      REQUIRE(spans[0].size() == 12U);
      REQUIRE(spans[1].size() == sizeof(instance));
      break;
    default:
      REQUIRE(false);
    }
    break;
  default:
    REQUIRE(false);
  }

  clockwork_logging::LiteCompressor decompressor{memory_resource};
  const auto compressed_data = copy_compressed_spans(spans);
  std::vector<std::byte> decompressed_data(sizeof(instance));
  REQUIRE(ok(decompressor.decompress(counts_checksum, data_checksum, compressed_data, decompressed_data)));
  REQUIRE(std::ranges::equal(decompressed_data, std::as_bytes(jewels::as_single_item_span(instance))));
}

TEST_CASE("FixedArray of Optional")
{
  const auto max_index = 2U;
  const auto index_mask = GENERATE(0x00U, 0x01U, 0x02U, 0x03U);
  CAPTURE(index_mask);

  const uint64_t element = 27U;

  Tappy<tests::FixedArrayOfOptional> instance;
  for (size_t index = 0; index < max_index; ++index)
  {
    if (((index + 1) & index_mask) != 0U)
    {
      instance.get_mutable_fixed_array_optional()[index] = element;
    }
  }

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const auto compressor =
    TachyonLiteCompressor::make_compressor<Tappy<tests::FixedArrayOfOptional>>(memory_resource, "test");
  uint64_t counts_checksum{};
  uint64_t data_checksum{};
  std::span<const std::span<const std::byte>> spans;
  compressor->compress(
    Out{spans}, Out{counts_checksum}, Out{data_checksum}, std::as_bytes(jewels::as_single_item_span(instance)));

  switch (index_mask)
  {
  case 0x00U:
    REQUIRE(spans.size() == 1U);
    REQUIRE(spans[0].size() == 12U);
    break;
  case 0x01U:
    [[fallthrough]];
  case 0x02U:
    REQUIRE(spans.size() == 2U);
    REQUIRE(spans[0].size() == 16U);
    REQUIRE(spans[1].size() == 2U * sizeof(uint64_t));
    break;
  case 0x03U:
    REQUIRE(spans.size() == 2U);
    REQUIRE(spans[0].size() == 12U);
    REQUIRE(spans[1].size() == sizeof(instance));
    break;
  default:
    REQUIRE(false);
  }

  clockwork_logging::LiteCompressor decompressor{memory_resource};
  const auto compressed_data = copy_compressed_spans(spans);
  std::vector<std::byte> decompressed_data(sizeof(instance));
  REQUIRE(ok(decompressor.decompress(counts_checksum, data_checksum, compressed_data, decompressed_data)));
  REQUIRE(std::ranges::equal(decompressed_data, std::as_bytes(jewels::as_single_item_span(instance))));
}

TEST_CASE("FixedArray of Optional VarArray")
{
  const auto max_index = 2U;
  const auto index_mask = GENERATE(0x00U, 0x01U, 0x02U, 0x03U);
  CAPTURE(index_mask);
  const auto array_size = GENERATE(0U, 1U, 2U);
  CAPTURE(array_size);

  const auto element = jewels::Uuid<int8_t>::random_uuid();

  Tappy<tests::FixedArrayOfOptionalVarArray> instance;
  for (size_t fixed_index = 0; fixed_index < max_index; ++fixed_index)
  {
    if (((fixed_index + 1) & index_mask) != 0U)
    {
      auto& maybe_array = instance.get_mutable_fixed_array_optional_var_array()[fixed_index];
      maybe_array.emplace();
      for (size_t array_index = 0; array_index < array_size; ++array_index)
      {
        maybe_array->emplace_back(element);
      }
    }
  }

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const auto compressor =
    TachyonLiteCompressor::make_compressor<Tappy<tests::FixedArrayOfOptionalVarArray>>(memory_resource, "test");
  uint64_t counts_checksum{};
  uint64_t data_checksum{};
  std::span<const std::span<const std::byte>> spans;
  compressor->compress(
    Out{spans}, Out{counts_checksum}, Out{data_checksum}, std::as_bytes(jewels::as_single_item_span(instance)));

  switch (index_mask)
  {
  case 0x00U:
    REQUIRE(spans.size() == 1U);
    REQUIRE(spans[0].size() == 12U);
    break;
  case 0x01U:
    [[fallthrough]];
  case 0x02U:
    switch (array_size)
    {
    case 0U:
      REQUIRE(spans.size() == 2U);
      if (index_mask == 0x01U)
      {
        REQUIRE(spans[0].size() == 20U);
      }
      else
      {
        REQUIRE(spans[0].size() == 16U);
      }
      REQUIRE(spans[1].size() == sizeof(uint64_t));
      break;
    case 1U:
      REQUIRE(spans.size() == 3U);
      REQUIRE(spans[0].size() == 24U);
      REQUIRE(spans[1].size() == sizeof(element));
      REQUIRE(spans[2].size() == 2U * sizeof(uint64_t));
      break;
    case 2U:
      REQUIRE(spans.size() == 2U);
      REQUIRE(spans[0].size() == 16U);
      REQUIRE(spans[1].size() == 2U * (sizeof(element) + sizeof(uint64_t)));
      break;
    default:
      REQUIRE(false);
    }
    break;
  case 0x03U:
    switch (array_size)
    {
    case 0U:
      REQUIRE(spans.size() == 3U);
      REQUIRE(spans[0].size() == 24U);
      REQUIRE(spans[1].size() == sizeof(uint64_t));
      REQUIRE(spans[2].size() == sizeof(uint64_t));
      break;
    case 1U:
      REQUIRE(spans.size() == 4U);
      REQUIRE(spans[0].size() == 28U);
      REQUIRE(spans[1].size() == sizeof(element));
      REQUIRE(spans[2].size() == (2U * sizeof(uint64_t)) + sizeof(element));
      REQUIRE(spans[3].size() == 2U * sizeof(uint64_t));
      break;
    case 2U:
      REQUIRE(spans.size() == 2U);
      REQUIRE(spans[0].size() == 12U);
      REQUIRE(spans[1].size() == sizeof(instance));
      break;
    default:
      REQUIRE(false);
    }
    break;
  default:
    REQUIRE(false);
  }

  clockwork_logging::LiteCompressor decompressor{memory_resource};
  const auto compressed_data = copy_compressed_spans(spans);
  std::vector<std::byte> decompressed_data(sizeof(instance));
  REQUIRE(ok(decompressor.decompress(counts_checksum, data_checksum, compressed_data, decompressed_data)));
  REQUIRE(std::ranges::equal(decompressed_data, std::as_bytes(jewels::as_single_item_span(instance))));
}

TEST_CASE("Tensor of VarArray")
{
  const auto var_array_size = GENERATE(0U, 1U, 2U, 3U);
  CAPTURE(var_array_size);

  const uint64_t element = 27U;

  Tappy<tests::TensorOfVarArray> instance;
  for (auto& var_array : instance.get_mutable_tensor_var_array().storage())
  {
    for (size_t i = 0; i < var_array_size; ++i)
    {
      var_array.emplace_back(element);
    }
  }

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const auto compressor =
    TachyonLiteCompressor::make_compressor<Tappy<tests::TensorOfVarArray>>(memory_resource, "test");
  uint64_t counts_checksum{};
  uint64_t data_checksum{};
  std::span<const std::span<const std::byte>> spans;
  compressor->compress(
    Out{spans}, Out{counts_checksum}, Out{data_checksum}, std::as_bytes(jewels::as_single_item_span(instance)));

  switch (var_array_size)
  {
  case 0U:
    REQUIRE(spans.size() == 1U);
    REQUIRE(spans[0].size() == 12U);
    break;
  case 1U:
    REQUIRE(spans.size() == 6U);
    REQUIRE(spans[0].size() == 11U * sizeof(int32_t));
    REQUIRE(spans[1].size() == sizeof(element));
    REQUIRE(spans[2].size() == sizeof(element) + sizeof(uint64_t));
    REQUIRE(spans[3].size() == sizeof(element) + sizeof(uint64_t));
    REQUIRE(spans[4].size() == sizeof(element) + sizeof(uint64_t));
    REQUIRE(spans[5].size() == sizeof(uint64_t));
    break;
  case 2U:
    [[fallthrough]];
  case 3U:
    REQUIRE(spans.size() == 2U);
    REQUIRE(spans[0].size() == 12U);
    REQUIRE(spans[1].size() == sizeof(instance));
    break;
  default:
    REQUIRE(false);
  }

  clockwork_logging::LiteCompressor decompressor{memory_resource};
  const auto compressed_data = copy_compressed_spans(spans);
  std::vector<std::byte> decompressed_data(sizeof(instance));
  REQUIRE(ok(decompressor.decompress(counts_checksum, data_checksum, compressed_data, decompressed_data)));
  REQUIRE(std::ranges::equal(decompressed_data, std::as_bytes(jewels::as_single_item_span(instance))));
}

TEST_CASE("FixedArray of VarString")
{
  const auto string_size = GENERATE(range(0U, 24U));
  CAPTURE(string_size);

  constexpr std::string_view test_string{"01234567890123456789012"};

  Tappy<tests::FixedArrayOfVarString> instance;
  for (auto& var_string : instance.get_mutable_fixed_array_of_varstring())
  {
    REQUIRE(var_string.try_set(test_string.substr(0U, string_size)));
  }

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const auto compressor =
    TachyonLiteCompressor::make_compressor<Tappy<tests::FixedArrayOfVarString>>(memory_resource, "test");
  uint64_t counts_checksum{};
  uint64_t data_checksum{};
  std::span<const std::span<const std::byte>> spans;
  compressor->compress(
    Out{spans}, Out{counts_checksum}, Out{data_checksum}, std::as_bytes(jewels::as_single_item_span(instance)));

  if (string_size == 0U)
  {
    REQUIRE(spans.size() == 1U);
    REQUIRE(spans[0].size() == 12U);
  }
  else if (string_size < 16U)
  {
    REQUIRE(spans.size() == 4U);
    REQUIRE(spans[0].size() == 28U);
    REQUIRE(spans[1].size() == string_size);
    REQUIRE(spans[2].size() == sizeof(uint64_t) + string_size);
    REQUIRE(spans[3].size() == sizeof(uint64_t));
  }
  else
  {
    REQUIRE(spans.size() == 2U);
    REQUIRE(spans[0].size() == 12U);
    REQUIRE(spans[1].size() == sizeof(instance));
  }

  clockwork_logging::LiteCompressor decompressor{memory_resource};
  const auto compressed_data = copy_compressed_spans(spans);
  std::vector<std::byte> decompressed_data(sizeof(instance));
  REQUIRE(ok(decompressor.decompress(counts_checksum, data_checksum, compressed_data, decompressed_data)));
  REQUIRE(std::ranges::equal(decompressed_data, std::as_bytes(jewels::as_single_item_span(instance))));
}

TEST_CASE("FixedSoa of UUID")
{
  Tappy<tests::FixedSoaWithUuidFields> instance;
  for (auto soa_element : instance.get_mutable_soa_field())
  {
    soa_element.set_uuid_field1(jewels::Uuid<int8_t>::random_uuid());
    soa_element.set_uuid_field2(jewels::Uuid<int8_t>::random_uuid());
  }

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const auto compressor =
    TachyonLiteCompressor::make_compressor<Tappy<tests::FixedSoaWithUuidFields>>(memory_resource, "test");
  uint64_t counts_checksum{};
  uint64_t data_checksum{};
  std::span<const std::span<const std::byte>> spans;
  compressor->compress(
    Out{spans}, Out{counts_checksum}, Out{data_checksum}, std::as_bytes(jewels::as_single_item_span(instance)));

  REQUIRE(spans.size() == 2U);
  REQUIRE(spans[0].size() == 12U);
  REQUIRE(spans[1].size() == sizeof(instance));

  clockwork_logging::LiteCompressor decompressor{memory_resource};
  const auto compressed_data = copy_compressed_spans(spans);
  std::vector<std::byte> decompressed_data(sizeof(instance));
  REQUIRE(ok(decompressor.decompress(counts_checksum, data_checksum, compressed_data, decompressed_data)));
  REQUIRE(std::ranges::equal(decompressed_data, std::as_bytes(jewels::as_single_item_span(instance))));
}

TEST_CASE("FixedSoa of VarArray")
{
  const auto array_size = GENERATE(0U, 1U, 2U, 3U);
  CAPTURE(array_size);

  const uint32_t element1 = 27U;
  const uint32_t element2 = 42U;

  Tappy<tests::FixedSoaWithVarArrayFields> instance;
  for (auto soa_element : instance.get_mutable_soa_field())
  {
    for (size_t i = 0U; i < array_size; ++i)
    {
      soa_element.get_underlying_var_array_field1().emplace_back(element1);
      soa_element.get_underlying_var_array_field2().emplace_back(element2);
    }
  }

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const auto compressor =
    TachyonLiteCompressor::make_compressor<Tappy<tests::FixedSoaWithVarArrayFields>>(memory_resource, "test");
  uint64_t counts_checksum{};
  uint64_t data_checksum{};
  std::span<const std::span<const std::byte>> spans;
  compressor->compress(
    Out{spans}, Out{counts_checksum}, Out{data_checksum}, std::as_bytes(jewels::as_single_item_span(instance)));

  switch (array_size)
  {
  case 0U:
    REQUIRE(spans.size() == 1U);
    REQUIRE(spans[0].size() == 12U);
    break;
  case 1U:
    REQUIRE(spans.size() == 6U);
    REQUIRE(spans[0].size() == 44U);
    REQUIRE(spans[1].size() == sizeof(uint32_t));
    REQUIRE(spans[2].size() == sizeof(uint64_t) + sizeof(uint32_t));
    REQUIRE(spans[3].size() == sizeof(uint64_t) + sizeof(uint32_t));
    REQUIRE(spans[4].size() == sizeof(uint64_t) + sizeof(uint32_t));
    REQUIRE(spans[5].size() == sizeof(uint64_t));
    break;
  case 2U:
    [[fallthrough]];
    ;
  case 3U:
    REQUIRE(spans.size() == 2U);
    REQUIRE(spans[0].size() == 12U);
    REQUIRE(spans[1].size() == sizeof(instance));
    break;
  default:
    REQUIRE(false);
  }

  clockwork_logging::LiteCompressor decompressor{memory_resource};
  const auto compressed_data = copy_compressed_spans(spans);
  std::vector<std::byte> decompressed_data(sizeof(instance));
  REQUIRE(ok(decompressor.decompress(counts_checksum, data_checksum, compressed_data, decompressed_data)));
  REQUIRE(std::ranges::equal(decompressed_data, std::as_bytes(jewels::as_single_item_span(instance))));
}

TEST_CASE("VarSoa of UUID")
{
  const auto soa_size = GENERATE(0U, 1U, 2U);
  CAPTURE(soa_size);

  Tappy<tests::VarSoaWithUuidFields> instance;
  for (size_t soa_index = 0U; soa_index < soa_size; ++soa_index)
  {
    auto soa_element = instance.get_mutable_soa_field().emplace_back();
    soa_element.set_uuid_field1(jewels::Uuid<int8_t>::random_uuid());
    soa_element.set_uuid_field2(jewels::Uuid<int8_t>::random_uuid());
  }

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const auto compressor =
    TachyonLiteCompressor::make_compressor<Tappy<tests::VarSoaWithUuidFields>>(memory_resource, "test");
  uint64_t counts_checksum{};
  uint64_t data_checksum{};
  std::span<const std::span<const std::byte>> spans;
  compressor->compress(
    Out{spans}, Out{counts_checksum}, Out{data_checksum}, std::as_bytes(jewels::as_single_item_span(instance)));

  switch (soa_size)
  {
  case 0U:
    REQUIRE(spans.size() == 1U);
    REQUIRE(spans[0].size() == 12U);
    break;
  case 1U:
    REQUIRE(spans.size() == 4U);
    REQUIRE(spans[0].size() == 28U);
    REQUIRE(spans[1].size() == sizeof(jewels::Uuid<int8_t>));
    REQUIRE(spans[2].size() == sizeof(jewels::Uuid<int8_t>));
    REQUIRE(spans[3].size() == sizeof(uint64_t));
    break;
  case 2U:
    REQUIRE(spans.size() == 2U);
    REQUIRE(spans[0].size() == 12U);
    REQUIRE(spans[1].size() == sizeof(instance));
    break;
  default:
    REQUIRE(false);
  }

  clockwork_logging::LiteCompressor decompressor{memory_resource};
  const auto compressed_data = copy_compressed_spans(spans);
  std::vector<std::byte> decompressed_data(sizeof(instance));
  REQUIRE(ok(decompressor.decompress(counts_checksum, data_checksum, compressed_data, decompressed_data)));
  REQUIRE(std::ranges::equal(decompressed_data, std::as_bytes(jewels::as_single_item_span(instance))));
}

TEST_CASE("VarSoa of VarArray")
{
  const auto soa_size = GENERATE(0U, 1U, 2U);
  CAPTURE(soa_size);
  const auto array_size = GENERATE(0U, 1U, 2U, 3U);
  CAPTURE(array_size);

  const uint32_t element1 = 27U;
  const uint32_t element2 = 42U;

  Tappy<tests::VarSoaWithVarArrayFields> instance;
  for (size_t soa_index = 0U; soa_index < soa_size; ++soa_index)
  {
    auto soa_element = instance.get_mutable_soa_field().emplace_back();
    for (size_t array_index = 0U; array_index < array_size; ++array_index)
    {
      soa_element.get_underlying_var_array_field1().emplace_back(element1);
      soa_element.get_underlying_var_array_field2().emplace_back(element2);
    }
  }

  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const auto compressor =
    TachyonLiteCompressor::make_compressor<Tappy<tests::VarSoaWithVarArrayFields>>(memory_resource, "test");
  uint64_t counts_checksum{};
  uint64_t data_checksum{};
  std::span<const std::span<const std::byte>> spans;
  compressor->compress(
    Out{spans}, Out{counts_checksum}, Out{data_checksum}, std::as_bytes(jewels::as_single_item_span(instance)));

  switch (soa_size)
  {
  case 0U:
    REQUIRE(spans.size() == 1U);
    REQUIRE(spans[0].size() == 12U);
    break;
  case 1U:
    switch (array_size)
    {
    case 0U:
      REQUIRE(spans.size() == 2U);
      REQUIRE(spans[0].size() == 16U);
      REQUIRE(spans[1].size() == sizeof(uint64_t));
      break;
    case 1U:
      REQUIRE(spans.size() == 6U);
      REQUIRE(spans[0].size() == 44U);
      REQUIRE(spans[1].size() == sizeof(uint32_t));
      REQUIRE(spans[2].size() == sizeof(uint64_t));
      REQUIRE(spans[3].size() == sizeof(uint32_t));
      REQUIRE(spans[4].size() == sizeof(uint64_t));
      REQUIRE(spans[5].size() == sizeof(uint64_t));
      break;
    case 2U:
      [[fallthrough]];
    case 3U:
      REQUIRE(spans.size() == 4U);
      REQUIRE(spans[0].size() == 28U);
      REQUIRE(spans[1].size() == (4U * sizeof(uint32_t)) + sizeof(uint64_t));
      REQUIRE(spans[2].size() == (4U * sizeof(uint32_t)) + sizeof(uint64_t));
      REQUIRE(spans[3].size() == sizeof(uint64_t));
      break;
    default:
      REQUIRE(false);
    }
    break;
  case 2U:
    switch (array_size)
    {
    case 0U:
      REQUIRE(spans.size() == 2U);
      REQUIRE(spans[0].size() == 16U);
      REQUIRE(spans[1].size() == sizeof(uint64_t));
      break;
    case 1U:
      REQUIRE(spans.size() == 6U);
      REQUIRE(spans[0].size() == 44U);
      REQUIRE(spans[1].size() == sizeof(uint32_t));
      REQUIRE(spans[2].size() == sizeof(uint64_t) + sizeof(uint32_t));
      REQUIRE(spans[3].size() == sizeof(uint64_t) + sizeof(uint32_t));
      REQUIRE(spans[4].size() == sizeof(uint64_t) + sizeof(uint32_t));
      REQUIRE(spans[5].size() == 2U * sizeof(uint64_t));
      break;
    case 2U:
      [[fallthrough]];
    case 3U:
      REQUIRE(spans.size() == 2U);
      REQUIRE(spans[0].size() == 12U);
      REQUIRE(spans[1].size() == sizeof(instance));
      break;
    default:
      REQUIRE(false);
    }
    break;
  default:
    REQUIRE(false);
  }

  clockwork_logging::LiteCompressor decompressor{memory_resource};
  const auto compressed_data = copy_compressed_spans(spans);
  std::vector<std::byte> decompressed_data(sizeof(instance));
  REQUIRE(ok(decompressor.decompress(counts_checksum, data_checksum, compressed_data, decompressed_data)));
  REQUIRE(std::ranges::equal(decompressed_data, std::as_bytes(jewels::as_single_item_span(instance))));
}

} // namespace
} // namespace clockwork::serialization
