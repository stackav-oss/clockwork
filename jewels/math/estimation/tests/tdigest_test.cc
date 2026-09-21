// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/math/estimation/tdigest.hh"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory_resource>
#include <random>

namespace jewels::math::tdigest
{
TEST_CASE("Single-point smoke test")
{
  auto memres = jewels::memory::MemoryResource(std::pmr::get_default_resource());
  constexpr float compression = 10.0;
  TDigest<QCubedPolicy<size_t>> tdigest(memres, compression);
  CHECK(tdigest.get_total_weight() == 0.0f);
  CHECK(tdigest.get_num_centroids() == 0);
  tdigest.add(1);
  CHECK(tdigest.get_total_weight() == 1.0);
  CHECK(tdigest.get_min() == 1);
  CHECK(tdigest.get_max() == 1);
  const auto centroids = tdigest.get_centroids(memres);
  REQUIRE(centroids.size() == 1);
  const auto& centroid = centroids[0];
  CHECK(centroid.sum == 1);
  CHECK(centroid.weight == 1.0);
  CHECK(centroid.mean == 1.0);
  tdigest.compress();
  CHECK(tdigest.get_total_weight() == 1.0);
  CHECK(tdigest.get_min() == 1);
  CHECK(tdigest.get_max() == 1);
  CHECK(tdigest.get_centroids(memres) == centroids);
  CHECK(tdigest.get_quantile(0.5) == 1);
}

TEST_CASE("Linear distribution (size_t), low resolution")
{
  auto memres = jewels::memory::MemoryResource(std::pmr::get_default_resource());
  constexpr float compression = 10.0;
  TDigest<QCubedPolicy<size_t>> tdigest(memres, compression);
  // Inserting compression * 20 elements takes us right up to the threshold where we compress
  for (size_t i = 0; i < 200; ++i)
  {
    tdigest.add(i);
  }
  CHECK(tdigest.get_min() == 0);
  CHECK(tdigest.get_max() == 199);
  CHECK(tdigest.get_total_weight() == 200);
  CHECK(tdigest.get_num_centroids() == 200);

  // Inserting 1 more element triggers a compression
  tdigest.add(0);
  CHECK(tdigest.get_total_weight() == 201);
  // Compression should result in number of bins between [compression/2, compression]
  CHECK(tdigest.get_num_centroids() >= 5);
  CHECK(tdigest.get_num_centroids() <= 10);
  CHECK(tdigest.get_min() == 0);
  CHECK(tdigest.get_max() == 199);

  const auto get_quantile = [&tdigest](float quantile)
  {
    const auto result = tdigest.get_quantile(quantile);
    REQUIRE(result);
    return *result;
  };

  CHECK(get_quantile(0.9999f) == Catch::Approx(199.98).margin(1.5));
  // Exactly max at 1.0
  CHECK(get_quantile(1.0f) == 199.0);
}

TEST_CASE("Linear distribution (size_t), high resolution")
{
  auto memres = jewels::memory::MemoryResource(std::pmr::get_default_resource());
  constexpr float compression = 100.0;
  TDigest<QCubedPolicy<size_t>> tdigest(memres, compression);
  for (size_t i = 0; i < 20000; ++i)
  {
    tdigest.add(i % 100);
  }
  tdigest.compress();
  CHECK(tdigest.get_min() == 0);
  CHECK(tdigest.get_max() == 99);
  CHECK(tdigest.get_total_weight() == 20000);
  // Compression should result in number of bins between [compression/2, compression]
  CHECK(tdigest.get_num_centroids() >= 50);
  CHECK(tdigest.get_num_centroids() <= 100);

  const auto get_quantile = [&tdigest](float quantile)
  {
    const auto result = tdigest.get_quantile(quantile);
    REQUIRE(result);
    return *result;
  };

  CHECK(get_quantile(0.5f) == Catch::Approx(50.0f).margin(1.0f));
  CHECK(get_quantile(0.75f) == Catch::Approx(75.0f).margin(0.4f));
  CHECK(get_quantile(0.99f) == Catch::Approx(99.0f).margin(0.1f));
  // Exactly max at 1.0
  CHECK(get_quantile(1.0f) == 99.0);
}

template <class Distribution>
struct Sampler
{
  std::mt19937 prng;
  Distribution dist;

  template <class SeedT, class... Args>
  explicit Sampler(SeedT seed, Args... args)
    : prng(seed), dist(args...)
  {
  }

  auto sample()
  {
    return dist(prng);
  }
};

TEST_CASE("Normal distribution (float)")
{
  auto memres = jewels::memory::MemoryResource(std::pmr::get_default_resource());
  constexpr float compression = 1000.0;
  TDigest<QCubedPolicy<float>> tdigest(memres, compression);

  // Normal distribution, seed=42, mean=0.0, stddev=1.0
  Sampler<std::normal_distribution<float>> sampler(42U, 0.0f, 1.0f);
  for (size_t i = 0; i < 200000; ++i)
  {
    tdigest.add(sampler.sample());
  }
  tdigest.compress();
  CHECK(tdigest.get_total_weight() == 200000);
  // Compression should result in number of bins between [compression/2, compression]
  CHECK(tdigest.get_num_centroids() >= 500);
  CHECK(tdigest.get_num_centroids() <= 1000);

  const auto get_quantile = [&tdigest](float quantile)
  {
    const auto result = tdigest.get_quantile(quantile);
    REQUIRE(result);
    return *result;
  };

  CHECK(get_quantile(0.16f) == Catch::Approx(-1.0f).margin(0.04f)); // -1 stddev
  CHECK(get_quantile(0.25f) == Catch::Approx(-0.6745f).margin(0.02f));
  CHECK(get_quantile(0.5f) == Catch::Approx(0.0f).margin(0.01f)); // median
  CHECK(get_quantile(0.75f) == Catch::Approx(0.6745f).margin(0.01f));
  CHECK(get_quantile(0.84f) == Catch::Approx(1.0f).margin(0.01f)); // +1 stddev
  CHECK(get_quantile(0.975f) == Catch::Approx(1.96f).margin(0.01f));
  CHECK(get_quantile(0.99f) == Catch::Approx(2.326f).margin(0.01f));
  // At extreme ends with only ~500 bins we can still be off, so margin ticks up again
  CHECK(get_quantile(0.999f) == Catch::Approx(3.09f).margin(0.15f));
}

} // namespace jewels::math::tdigest
