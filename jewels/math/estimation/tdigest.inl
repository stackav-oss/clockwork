// IWYU pragma: private, include "jewels/math/estimation/tdigest.hh"
#pragma once
#include "jewels/math/estimation/tdigest.hh"

#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <limits>
#include <sys/types.h>
#include <vector>

namespace jewels::math::tdigest
{

template <class Policies>
TDigest<Policies>::TDigest(jewels::memory::MemoryResource memres, float compression)
  : memres_{memres},
    compression_{compression},
    min_{std::numeric_limits<ValueT>::max()},
    max_{std::numeric_limits<ValueT>::min()},
    sums_(memres),
    weights_(memres_),
    means_(memres),
    capacities_(memres)
{
  const auto reserve = max_centroids();
  sums_.reserve(reserve);
  weights_.reserve(reserve);
  means_.reserve(reserve);
  capacities_.reserve(reserve);
}

template <class Policies>
void TDigest<Policies>::add(ValueT value)
{
  return add(value, 1);
}

template <class Policies>
void TDigest<Policies>::add(ValueT value, WeightT weight)
{
  min_ = std::min(min_, value);
  max_ = std::max(max_, value);

  if (means_.empty())
  {
    sums_.emplace_back(value);
    weights_.emplace_back(weight);
    total_weight_ += weight;
    means_.emplace_back(static_cast<MeanT>(value) / weight);
    // We just start this off full; it will be fixed during the first compression.
    capacities_.emplace_back(1);
    return;
  }

  // Find the largest centroid below this value.
  // We belong in this one, or the one after it, or in a new centroid in between the two.
  const auto itr = std::lower_bound(means_.begin(), means_.end(), static_cast<MeanT>(value));
  // This is guaranteed non-negative because lower_bound can't return before begin()
  const auto idx = static_cast<size_t>(std::distance(means_.begin(), itr));

  bool merged = false;
  // When adding, we use an optimization where we conservatively estimate the
  // capacity of centroids based on their capacity at the most recent full
  // compression (which is where we calculate them for real).  In general,
  // capacities *increase* as the total weight (number of samples) increases, so
  // this is conservative.  It prevents us from having to do O(N) insertions of
  // single-element new centroids on every sample, or else maintaining a list of
  // cumulative weights at each centroid, which is also O(N) on every addition.
  // This gives us O(1) additions whenever the existing centroids in the
  // vicinity of the new data point have capacity.  Occasionally, as centroids
  // exceed their conservative capacity estimates, we'll start inserting new
  // centroids at O(N) cost, and as those fill up we'll eventually run a full
  // compression at O(N) cost.
  //
  // When we do create a new centroid, we set its capacity to the minimum of the
  // capacities of its two neighbors.  This is also conservative, assuming the
  // k-function does not have a strange shape.  (If the first derivative of the
  // k-function has a local maximum at the location of the centroid it might not
  // be conservative, but won't be too far off in practice.  And our k-function
  // does not do that.)
  auto min_capacity = std::numeric_limits<WeightT>::max();

  if (idx > 0)
  {
    // Try the bin to the left
    const auto prev_idx = static_cast<size_t>(idx - 1);
    merged = try_merge(prev_idx, value, weight);
    min_capacity = std::min(min_capacity, capacities_[prev_idx]);
  }

  if (!merged && idx < means_.size())
  {
    // Try the bin to the right
    merged = try_merge(idx, value, weight);
    min_capacity = std::min(min_capacity, capacities_[idx]);
  }

  if (!merged)
  {
    // No room on either side; we must insert a new centroid.
    if (idx == 0 || idx == means_.size())
    {
      // At the beginning and end we ensure the capacity is small to be conservative.
      min_capacity = 1;
    }
    const auto sidx = static_cast<ssize_t>(idx);
    sums_.insert(sums_.begin() + sidx, value);
    weights_.insert(weights_.begin() + sidx, weight);
    total_weight_ += weight;
    means_.insert(means_.begin() + sidx, static_cast<MeanT>(value) / weight);
    capacities_.insert(capacities_.begin() + sidx, min_capacity);

    if (sums_.size() >= max_centroids())
    {
      compress();
    }
  }
}

template <class Policies>
bool TDigest<Policies>::try_merge(std::size_t index, ValueT value, WeightT weight)
{
  if (weights_[index] + weight < capacities_[index])
  {
    sums_[index] += value;
    weights_[index] += weight;
    total_weight_ += weight;
    means_[index] = static_cast<MeanT>(sums_[index]) / weights_[index];
    return true;
  }
  return false;
}

template <class Policies>
void TDigest<Policies>::compress()
{
  if (sums_.size() < 2)
  {
    return;
  }

  std::pmr::vector<ValueT> new_sums(memres_);
  std::pmr::vector<WeightT> new_weights(memres_);
  std::pmr::vector<MeanT> new_means(memres_);
  std::pmr::vector<WeightT> new_capacities(memres_);

  const auto reserve = max_centroids();
  new_sums.reserve(reserve);
  new_weights.reserve(reserve);
  new_means.reserve(reserve);
  new_capacities.reserve(reserve);

  // cur_sum/cur_weight hold the centroid being accumulated; initially the first one.
  auto cur_sum = sums_[0];
  auto cur_weight = weights_[0];
  // cumulative_weight is all of the weight of all centroids *before* the current one being accumulated.
  WeightT cumulative_weight = 0;

  // Calculate how much weight the current centroid is allowed to accumulate
  const auto calc_capacity = [&cumulative_weight, this]()
  {
    const auto qval0 = float{cumulative_weight} / float{total_weight_};
    const auto kval0 = calc_k(qval0);
    // The bound on centroid width (weight) is a k-size of 1; so our upper limit on k is:
    const auto kval1 = kval0 + 1;
    // From that we back into the upper limit on q; calc_q is the inverse of calc_k:
    const auto qval1 = calc_q(kval1);
    // And from that the weight limit:
    const auto max_cumulative_weight = qval1 * total_weight_;
    // So finally, capacity is how large we can grow the current centroid until its k-size would be greater than 1:
    const auto capacity = max_cumulative_weight - cumulative_weight;
    return capacity;
  };
  WeightT capacity = calc_capacity();

  for (size_t i = 1; i < sums_.size(); ++i)
  {
    const auto combined_weight = cur_weight + weights_[i];
    if (combined_weight <= capacity)
    {
      // Accumulate centroid i into current one.
      cur_weight = combined_weight;
      cur_sum = cur_sum + sums_[i];
    }
    else
    {
      // Push current centroid and start a new accumulation with centroid i.
      new_sums.emplace_back(cur_sum);
      new_weights.emplace_back(cur_weight);
      new_means.emplace_back(static_cast<MeanT>(cur_sum) / cur_weight);
      new_capacities.emplace_back(capacity);
      cumulative_weight += cur_weight;
      cur_sum = sums_[i];
      cur_weight = weights_[i];
      capacity = calc_capacity();
    }
  }
  // Push final centroid
  const auto qval0 = float{cumulative_weight} / float{total_weight_};
  const auto qval1 = float{cumulative_weight + cur_weight} / float{total_weight_};
  const auto kval0 = calc_k(qval0);
  const auto kval1 = calc_k(qval1);
  new_capacities.emplace_back(kval1 - kval0);
  new_sums.emplace_back(cur_sum);
  new_weights.emplace_back(cur_weight);
  new_means.emplace_back(static_cast<MeanT>(cur_sum) / cur_weight);

  // Swap containers
  std::swap(sums_, new_sums);
  std::swap(weights_, new_weights);
  std::swap(means_, new_means);
  std::swap(capacities_, new_capacities);
}

template <class Policies>
size_t TDigest<Policies>::max_centroids()
{
  return static_cast<size_t>(size_factor * compression_) + 1;
}

template <class Policies>
auto TDigest<Policies>::get_centroids(jewels::memory::MemoryResource memres) const -> std::pmr::vector<CentroidT>
{
  std::pmr::vector<CentroidT> result(memres);
  result.reserve(sums_.size());
  for (size_t i = 0; i < sums_.size(); ++i)
  {
    result.emplace_back(CentroidT{.sum = sums_[i], .weight = weights_[i], .mean = means_[i]});
  }
  return result;
}

template <class Policies>
[[nodiscard]] auto TDigest<Policies>::get_quantile(float quantile) const -> jewels::expected<MeanT, jewels::MonoError>
{
  if (get_num_centroids() == 0)
  {
    return jewels::unexpected(jewels::MonoError{});
  };

  if (quantile <= 0.0)
  {
    return min_;
  }
  if (quantile >= 1.0)
  {
    return max_;
  }

  const auto target_weight = quantile * total_weight_;
  WeightT cumulative_weight = 0;

  for (size_t i = 0; i < get_num_centroids(); ++i)
  {
    cumulative_weight += weights_[i];
    if (cumulative_weight >= target_weight)
    {
      return means_[i];
    }
  }
  return max_;
}

} // namespace jewels::math::tdigest
