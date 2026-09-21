// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/callsig/outparam.hh"
#include "jewels/memory/instrumented_pmr_resource.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/new_delete_memory_resource.hh"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <memory_resource>
#include <utility>
#include <vector>

namespace jewels::memory
{
TEST_CASE("new delete memory resource | basic")
{
  NewDeleteMemoryResource pool1(1024, "pool1");
  NewDeleteMemoryResource pool2(1024, "pool2");
  const MemoryResource res1(&pool1);
  const MemoryResource res2(&pool2);
  MemoryResourceMetrics metrics;

  {
    std::pmr::vector<int> vec1(res1);
    pool1.get_memory_resource_metrics(jewels::Out(metrics));
    CHECK(metrics.current_allocated == 0);
    vec1.reserve(100);
    pool1.get_memory_resource_metrics(jewels::Out(metrics));
    CHECK(metrics.current_allocated == 100 * sizeof(int));
    pool2.get_memory_resource_metrics(jewels::Out(metrics));
    CHECK(metrics.current_allocated == 0);
    {
      const std::pmr::vector<int> vec1a = std::move(vec1);
      pool1.get_memory_resource_metrics(jewels::Out(metrics));
      CHECK(metrics.current_allocated == 100 * sizeof(int));
    }
    pool1.get_memory_resource_metrics(jewels::Out(metrics));
    CHECK(metrics.current_allocated == 0);
  }
  pool1.get_memory_resource_metrics(jewels::Out(metrics));
  CHECK(metrics.current_allocated == 0);
  pool2.get_memory_resource_metrics(jewels::Out(metrics));
  CHECK(metrics.current_allocated == 0);

  pool1.get_memory_resource_metrics(jewels::Out(metrics));
  CHECK(metrics.total_allocated == 100 * sizeof(int));
  CHECK(metrics.total_deallocated == 100 * sizeof(int));
  pool1.reset_incremental_metrics();
  pool1.get_memory_resource_metrics(jewels::Out(metrics));
  CHECK(metrics.total_allocated == 0);
  CHECK(metrics.total_deallocated == 0);
}

TEST_CASE("new delete memory resource | peak")
{
  constexpr size_t size = 64;
  NewDeleteMemoryResource pool(1024, "pool");
  const MemoryResource res(&pool);
  MemoryResourceMetrics metrics;

  using blob = std::pmr::vector<std::byte>;
  std::vector<blob> blobs{};

  pool.get_memory_resource_metrics(jewels::Out(metrics));
  CHECK(metrics.current_allocated == 0);
  CHECK(metrics.peak_allocated == 0);
  blobs.emplace_back(size, res);
  pool.get_memory_resource_metrics(jewels::Out(metrics));
  CHECK(metrics.current_allocated == size);
  CHECK(metrics.peak_allocated == size);
  blobs.clear();
  pool.get_memory_resource_metrics(jewels::Out(metrics));
  CHECK(metrics.current_allocated == 0);
  CHECK(metrics.peak_allocated == size);
  blobs.emplace_back(size, res);
  blobs.emplace_back(size, res);
  pool.get_memory_resource_metrics(jewels::Out(metrics));
  CHECK(metrics.current_allocated == 2 * size);
  CHECK(metrics.peak_allocated == 2 * size);
  blobs.emplace_back(1, res);
  pool.get_memory_resource_metrics(jewels::Out(metrics));
  CHECK(metrics.current_allocated == (2 * size) + 1);
  CHECK(metrics.peak_allocated == (2 * size) + 1);
  blobs.emplace_back(size, res);
  pool.get_memory_resource_metrics(jewels::Out(metrics));
  CHECK(metrics.current_allocated == (3 * size) + 1);
  CHECK(metrics.peak_allocated == (3 * size) + 1);
  blobs.emplace_back(10, res);
}

} // namespace jewels::memory
