// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/monitor_resource.hh"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <memory_resource>
#include <utility>
#include <vector>

namespace jewels::memory
{
TEST_CASE("monitor resource | basic")
{
  jewels::memory::MonitorResource pool1;
  jewels::memory::MonitorResource pool2;
  const jewels::memory::MemoryResource res1(&pool1);
  const jewels::memory::MemoryResource res2(&pool2);

  {
    std::pmr::vector<int> vec1(res1);
    CHECK(pool1.used() == 0);
    vec1.reserve(100);
    CHECK(pool1.used() == 100 * sizeof(int));
    CHECK(pool2.used() == 0);
    {
      const std::pmr::vector<int> vec1a = std::move(vec1);
      CHECK(pool1.used() == 100 * sizeof(int));
    }
    CHECK(pool1.used() == 0);
  }
  CHECK(pool1.used() == 0);
  CHECK(pool2.used() == 0);
}

TEST_CASE("monitor resource | peak")
{
  constexpr size_t size = MonitorResource::warn_delta;
  constexpr size_t limit = 2 * size;
  MonitorResource pool(limit);
  const MemoryResource res(&pool);

  using blob = std::pmr::vector<std::byte>;
  std::vector<blob> blobs{};

  CHECK(pool.used() == 0);
  CHECK(pool.peak() == 0);
  CHECK(pool.threshold() == limit);
  blobs.emplace_back(size, res);
  CHECK(pool.used() == size);
  CHECK(pool.peak() == size);
  CHECK(pool.threshold() == limit);
  blobs.clear();
  CHECK(pool.used() == 0);
  CHECK(pool.peak() == size);
  CHECK(pool.threshold() == limit);
  blobs.emplace_back(size, res);
  blobs.emplace_back(size, res);
  CHECK(pool.used() == 2 * size);
  CHECK(pool.peak() == 2 * size);
  CHECK(pool.threshold() == limit);
  blobs.emplace_back(1, res);
  CHECK(pool.used() == 2 * size + 1);
  CHECK(pool.peak() == 2 * size + 1);
  CHECK(pool.threshold() == 2 * size + 1 + size);
  blobs.emplace_back(size, res);
  CHECK(pool.used() == 3 * size + 1);
  CHECK(pool.peak() == 3 * size + 1);
  CHECK(pool.threshold() == 2 * size + 1 + size);
  blobs.emplace_back(10, res);
  CHECK(pool.threshold() == 3 * size + 1 + 10 + size);
}

} // namespace jewels::memory
