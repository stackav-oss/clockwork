// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/zstd_helper.hh"
#include "jewels/memory/memory_resource.hh"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstring>
#include <memory_resource>
#include <span>
#include <vector>

namespace clockwork_logging
{
namespace
{

TEST_CASE("compress/decompress")
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};

  constexpr auto data_size = 1234U;
  const std::pmr::vector<std::byte> data(data_size, std::byte{'1'}, memory_resource);

  const auto compress_result = zstd_compress(std::span{data}, memory_resource);
  REQUIRE(compress_result);
  const auto& compress_out = compress_result.value();
  const auto decompress_result = zstd_decompress(std::span{compress_out}, memory_resource);
  REQUIRE(decompress_result);
  const auto& decompress_out = decompress_result.value();
  REQUIRE(decompress_out.size() == data_size);
  REQUIRE(std::memcmp(data.data(), decompress_out.data(), data_size) == 0);
}

} // namespace
} // namespace clockwork_logging
