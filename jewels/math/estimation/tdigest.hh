// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

/// Implementation of the t-digest algorithm for online estimation of
/// distributions.
///
/// The original t-digest paper was available at
/// https://github.com/tdunning/t-digest/blob/main/docs/t-digest-paper/histo.pdf
/// at the time this code was written.
///
/// T-digest is a distribution estimation algorithm that does not require any
/// prior knowledge of the distribution, such as the expected maximum value.  It
/// accumulates a minimum and maximum with precision, and estimates quantiles
/// (percentiles) in between using a dynamic and continuously-adjusted binning
/// mechanism.  The resolution of the distribution is controlled over the
/// quantile range by a customizable "k-scaling function" and a "compression"
/// parameter.  The default policy class provided here implements a q^3 scaling
/// function which provides the best resolution on the high end (quantiles near
/// 1.0) and lowest resolution near 0.0.  (The original t-digest paper used an
/// asin function to provide high resolution at both extremes.)
///
/// The compression parameter, counterintuitively, provides highest compression
/// at low values and lowest compression at high values.  This is how it was
/// used in the t-digest paper so we stick with that in our implementation.  In
/// fact, the "compression" parameter roughly controls the number of bins used
/// to accumulate, but this is not precisely controlled; the number of bins
/// varies dynamically.  But a small compression generally means fewer bins and
/// therefore a coarser distribution approximation, and large compression allows
/// more bins and greater resolution.
///
/// The basic t-digest algorithm is quite slow despite being among the fastest
/// of this type of algorithm; it's O(N) on every new observation where N is the
/// number of bins.  This implementation includes some optimizations to incur
/// that O(N) cost only occasionally; the overall complexity is still
/// technically O(N) per observation, but it's O(log N) on most observations and
/// O(N) at most every 20*compression observations, often much less frequently.
///
/// The magic number "20" is defined as the TDigest::size_factor constexpr, and
/// it determines the maximum number of bins prior to compressing.
#pragma once

#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"

#include <cmath>
#include <cstddef>
#include <vector>

namespace jewels::math::tdigest
{

/// Policy class for a k-scaling function of q^3 and single-precision floats
///
/// @tparam ValueType The type to use for observed values; this must be able to
/// hold a running sum of all observed values; the code does not check for
/// overflow.
template <class ValueType>
struct QCubedPolicy
{
  using ValueT = ValueType;
  using WeightT = float;
  using MeanT = float;

  /// The k-scaling function.
  ///
  /// This determines the distribution of bin sizes.  It's a function of q, the
  /// quintile (range [0.0, 1.0]), and the "compression" factor.  The latter
  /// value determines roughly the number of bins; lower compression means
  /// smaller number of bins (and less resolution).  The k-function must be
  /// uniformly increasing (positive first derivative) over the range [0.0,
  /// 1.0].  Where the first derivative is small (low slope of k), bins will be
  /// large (many observations per bin, low resolution), and where the slope is
  /// steeper, bins will be narrower (fewer observations, higher resolution).
  ///
  /// The result should be scaled by compression to return a total range in k
  /// of roughly compression/2 over the range [0,1] of q-values.
  ///
  /// In this implementation we use q^3 to provide highest resolution near 1.0.
  static float calc_k(float q_val, float compression)
  {
    return (compression / 2) * q_val * q_val * q_val;
  }

  /// The inverse of the k-scaling function
  ///
  /// This function must be the inverse of calc_k, within the limits of rounding
  /// error.
  static float calc_q(float k_val, float compression)
  {
    return std::cbrt(2 * k_val / compression);
  }
};

/// A structure for returning details about centroids
///
/// Each centroid is a "bin" where observations are accumulated.  The t-digest
/// algorithm dynamically sizes and places these bins to fit the observed range
/// of the data.  This structure is not used internally but only to return the
/// bin details from the TDigest interface.
template <class Policies>
struct Centroid
{
  using ValueT = Policies::ValueT;
  using WeightT = Policies::WeightT;
  using MeanT = Policies::MeanT;

  ValueT sum;
  WeightT weight;
  MeanT mean;

  bool operator==(const Centroid& other) const
  {
    return other.sum == sum && other.weight == weight && other.mean == mean;
  }
};

/// The t-digest algorithm implementation
///
/// Each instance of this class can accumulate observed values and estimate their distribution.
template <class Policies>
class TDigest
{
public:
  using ValueT = Policies::ValueT;
  using WeightT = Policies::WeightT;
  using MeanT = Policies::MeanT;
  using CentroidT = Centroid<Policies>;

  /// Construct using the given memory resource and compression value.
  ///
  /// The number of bins at any give time will be between 0 and compression*20.
  /// After a compression cycle (which happens automatically or when triggered
  /// with compress()), the number of bins will typically be in [compression/2,
  /// compression].
  ///
  /// Higher values for compression use more memory but produce a more detailed
  /// distribution.
  TDigest(jewels::memory::MemoryResource memres, float compression);

  /// Add an observation with weight 1
  void add(ValueT value);

  /// Add a weighted observation
  void add(ValueT value, WeightT weight);

  /// Get the total amount of accumulated weight (observations)
  [[nodiscard]] WeightT get_total_weight() const
  {
    return total_weight_;
  }

  /// Get the current number of centroids (bins)
  [[nodiscard]] size_t get_num_centroids() const
  {
    return sums_.size();
  }

  /// Get the minimum observed value
  ///
  /// This is always exact, not estimated.
  [[nodiscard]] ValueT get_min() const
  {
    return min_;
  }

  /// Get the maximum observed value
  ///
  /// This is always exact, not estimated.
  [[nodiscard]] ValueT get_max() const
  {
    return max_;
  }

  /// Get an estimated quantile value
  ///
  /// @param quantile Desired quantile (percentile) in range [0.0, 1.0]
  [[nodiscard]] jewels::expected<MeanT, jewels::MonoError> get_quantile(float quantile) const;

  /// Get a copy of the centroids
  ///
  /// This creates a new vector<Centroid> and returns it; the caller owns the memory.
  /// Usually you'd call compress() prior to calling this.
  [[nodiscard]] std::pmr::vector<CentroidT> get_centroids(jewels::memory::MemoryResource memres) const;

  /// Compress the centroids
  ///
  /// This is called automatically when necessary during accumulation, and
  /// should also be called manually to finalize the distribution once all data
  /// is accumulated.  There is no harm in calling it multiple times, but it is
  /// O(N) in the number of centroids so should not be called after every sample.
  void compress();

private:
  bool try_merge(std::size_t index, ValueT value, WeightT weight);
  float calc_k(float q_val)
  {
    return Policies::calc_k(q_val, compression_);
  }
  float calc_q(float k_val)
  {
    return Policies::calc_q(k_val, compression_);
  }
  size_t max_centroids();

  jewels::memory::MemoryResource memres_;
  float compression_;
  static constexpr float size_factor = 20.0;

  ValueT min_;
  ValueT max_;
  WeightT total_weight_{0};
  std::pmr::vector<ValueT> sums_;
  std::pmr::vector<WeightT> weights_;
  std::pmr::vector<MeanT> means_;
  std::pmr::vector<WeightT> capacities_;
};

} // namespace jewels::math::tdigest

#include "jewels/math/estimation/tdigest.inl"
