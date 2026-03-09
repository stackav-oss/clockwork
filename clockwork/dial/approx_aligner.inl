// IWYU pragma: private, include "clockwork/dial/approx_aligner.hh"
#pragma once

#include "clockwork/dial/approx_aligner.hh"

#include "clockwork/dial/alignment_type_clk_cc.hh"
#include "clockwork/dial/approx_aligner_config_clk_cc.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"

#include <chrono>
#include <compare>
#include <cstddef>
#include <iterator>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

namespace clockwork
{

template <template <typename...> typename PolicyType, typename... InputPolicies>
constexpr auto ApproxAligner<PolicyType, InputPolicies...>::find_alignment(
  jewels::memory::MemoryResource resource,
  const Config& config,
  const State& state,
  const InputTuple& inputs,
  SyncTime now) -> Result
{
  // Ensure minimum amount of time has elapsed.

  const auto elapsed_time = now - state.get_last_commit_timestamp();
  if (elapsed_time < config.get_minimum_wait_time())
  {
    return Result{
      .time_of_validity = now,
      .state = ApproxAlignerStateType::idle,
      .type = AlignmentType::none,
      .alignment = {},
    };
  }

  // Validate the inputs.

  const auto inputs_valid = Policy::validate_inputs(inputs);
  if (!inputs_valid)
  {
    return Result{
      .time_of_validity = now,
      .state = ApproxAlignerStateType::invalid_inputs,
      .type = AlignmentType::none,
      .alignment = {},
    };
  }

  // Extract the input values.

  auto values_array = extract_values(resource, inputs);

  // Attempt to find a full alignment.

  auto [full_alignment, timeout_alignment] = find_full_alignment(config, inputs, values_array);

  if (full_alignment)
  {
    return Result{
      .time_of_validity = now,
      .state = ApproxAlignerStateType::aligned,
      .type = AlignmentType::full,
      .alignment = std::move(full_alignment),
    };
  }

  // No full alignment, return insuffient data if we have not exceeded the max timeout.

  if (!config.has_maximum_wait_time() || (elapsed_time < std::chrono::nanoseconds(config.value_maximum_wait_time())))
  {
    return Result{
      .time_of_validity = now,
      .state = ApproxAlignerStateType::insufficient_data,
      .type = AlignmentType::none,
      .alignment = {},
    };
  }

  // Exceeded the max timeout, return the best partial alignment.

  if (timeout_alignment)
  {
    return Result{
      .time_of_validity = now,
      .state = ApproxAlignerStateType::timeout,
      .type = AlignmentType::partial,
      .alignment = std::move(timeout_alignment),
    };
  }

  // No partial alignment found (i.e. no inputs at all).

  return Result{
    .time_of_validity = now,
    .state = ApproxAlignerStateType::timeout,
    .type = AlignmentType::none,
    .alignment = {},
  };
}

template <template <typename...> typename PolicyType, typename... InputPolicies>
constexpr void
ApproxAligner<PolicyType, InputPolicies...>::commit(State& state, InputTuple& inputs, const Result& result)
{
  state.set_last_commit_timestamp(result.time_of_validity);

  if (result.alignment)
  {
    std::apply(
      [&result](auto&... input)
      {
        std::apply(
          [&input...](const auto&... itr)
          { (input.set_cursor((itr == input.get_view().end()) ? itr : std::next(itr)), ...); },
          result.alignment->inputs);
      },
      inputs);
  }
}

template <template <typename...> typename PolicyType, typename... InputPolicies>
constexpr void ApproxAligner<PolicyType, InputPolicies...>::reset(State& state, InputTuple& inputs, SyncTime now)
{
  state.set_last_commit_timestamp(now);

  std::apply([](auto&... input) { (input.set_cursor(input.get_view().begin()), ...); }, inputs);
}

template <template <typename...> typename PolicyType, typename... InputPolicies>
constexpr void ApproxAligner<PolicyType, InputPolicies...>::clear(
  State& state, InputTuple& inputs, SyncTime now, const InputItTuple& new_begins)
{
  state.set_last_commit_timestamp(now);

  std::apply(
    [&new_begins](auto&... input)
    { std::apply([&input...](const auto&... begin) { (input.set_cursor(begin), ...); }, new_begins); },
    inputs);
}

template <template <typename...> typename PolicyType, typename... InputPolicies>
constexpr auto ApproxAligner<PolicyType, InputPolicies...>::find_full_alignment(
  const Config& config, const InputTuple& inputs, const ValueVectorsArray& values_array)
  -> std::tuple<std::optional<Alignment>, std::optional<Alignment>>
{
  struct IndexAlignment
  {
    ValueType score;
    IndexArray indices;
  };
  auto below_thres_alignment = std::optional<IndexAlignment>();
  auto above_thres_alignment = std::optional<IndexAlignment>();

  auto generator =
    detail::CombinationGenerator<ValueType, input_count>(jewels::memory::make_non_null_from_ref(values_array));

  for (auto it = generator.begin(); it != generator.end(); ++it)
  {
    auto score = Policy::objective(it.value_ptrs());
    if (static_cast<double>(score) <= config.get_minimum_score_threshold())
    {
      if (
        !below_thres_alignment || (Policy::less_than(it.indices(), below_thres_alignment->indices) ||
                                   (config.get_find_type() == ApproxAlignerFindType::last)))
      {
        below_thres_alignment = IndexAlignment{
          .score = score,
          .indices = it.indices(),
        };
      }
    }
    else if (
      !above_thres_alignment || (score < above_thres_alignment->score) ||
      ((score == above_thres_alignment->score) && (Policy::less_than(it.indices(), above_thres_alignment->indices) ||
                                                   (config.get_find_type() == ApproxAlignerFindType::last))))
    {
      above_thres_alignment = IndexAlignment{
        .score = score,
        .indices = it.indices(),
      };
    }
  }

  auto make_alignment = [&inputs](const std::optional<IndexAlignment>& alignment) -> std::optional<Alignment>
  {
    if (!alignment)
    {
      return {};
    }
    return Alignment{
      .score = alignment->score,
      .inputs = [&inputs, &alignment]<size_t... idx>(std::index_sequence<idx...>)
      { return InputItTuple(std::next(std::get<idx>(inputs).get_cursor(), std::get<idx>(alignment->indices))...); }(
        std::make_index_sequence<input_count>{}),
    };
  };

  return {make_alignment(below_thres_alignment), make_alignment(above_thres_alignment)};
}

template <template <typename...> typename PolicyType, typename... InputPolicies>
constexpr auto ApproxAligner<PolicyType, InputPolicies...>::extract_values(
  jewels::memory::MemoryResource resource, const InputTuple& inputs) -> ValueVectorsArray
{
  auto extract = [&resource]<typename InputPolicy>(const InputType<InputPolicy>& input)
  {
    auto values = std::pmr::vector<ValueType>(resource);
    for (auto it = input.get_cursor(); it != input.end(); ++it)
    {
      values.emplace_back(InputPolicy::get_value(*it));
    }
    return values;
  };

  return std::apply(
    [&extract](auto&... input) -> ValueVectorsArray { return {extract.template operator()<InputPolicies>(input)...}; },
    inputs);
}

namespace detail
{

template <typename ValueType, size_t input_count>
CombinationGenerator<ValueType, input_count>::CombinationGenerator(
  jewels::memory::ObjectPtr<const ValueVectorsArray> inputs)
  : inputs_(inputs)
{
}

template <typename ValueType, size_t input_count>
auto CombinationGenerator<ValueType, input_count>::begin() -> Iterator
{
  return Iterator(this, false);
}

template <typename ValueType, size_t input_count>
auto CombinationGenerator<ValueType, input_count>::end() -> Iterator
{
  return Iterator(this, true);
}

template <typename ValueType, size_t input_count>
CombinationGenerator<ValueType, input_count>::Iterator::Iterator(const CombinationGenerator* generator, bool done)
  : generator_(generator), done_(done)
{
  for (size_t i = 0; i < input_count; ++i)
  {
    iterators_.at(i) = generator_->inputs_->at(i).begin();
    if (generator_->inputs_->at(i).begin() != generator_->inputs_->at(i).end())
    {
      value_ptrs_.at(i) = &(*iterators_.at(i));
    }
    else
    {
      done_ = true;
    }
  }
}

template <typename ValueType, size_t input_count>
bool CombinationGenerator<ValueType, input_count>::Iterator::operator==(const Iterator& rhs) const
{
  return (generator_ == rhs.generator_ && done_ == rhs.done_);
}

template <typename ValueType, size_t input_count>
bool CombinationGenerator<ValueType, input_count>::Iterator::operator!=(const Iterator& rhs) const
{
  return !(*this == rhs);
}

template <typename ValueType, size_t input_count>
auto CombinationGenerator<ValueType, input_count>::Iterator::value_ptrs() const -> const ValuePtrArray&
{
  return value_ptrs_;
}

template <typename ValueType, size_t input_count>
auto CombinationGenerator<ValueType, input_count>::Iterator::indices() const -> IndexArray
{
  auto indices = IndexArray{};
  for (size_t i = 0; i < input_count; ++i)
  {
    indices.at(i) = std::distance(generator_->inputs_->at(i).begin(), iterators_.at(i));
  }
  return indices;
}

template <typename ValueType, size_t input_count>
auto CombinationGenerator<ValueType, input_count>::Iterator::operator++() -> Iterator&
{
  if (done_)
  {
    return *this;
  }

  // Find the rightmost input iterator that can be incremented.

  size_t index = input_count;
  while (index > 0 && (iterators_.at(index - 1) == generator_->inputs_->at(index - 1).end() ||
                       std::next(iterators_.at(index - 1)) == generator_->inputs_->at(index - 1).end()))
  {
    --index;
  }

  // If there are none (i.e. index == 0), then there are no more combinations.

  if (index == 0)
  {
    done_ = true;
    return *this;
  }

  // Increment the input iterator.

  ++iterators_.at(index - 1);

  // Reset all input iterators after the indexed one to the begin iterator.

  for (size_t i = index; i < input_count; ++i)
  {
    iterators_.at(i) = generator_->inputs_->at(i).begin();
  }

  // Populate the value ptrs array.

  for (size_t i = 0; i < input_count; ++i)
  {
    value_ptrs_.at(i) = &(*iterators_.at(i));
  }

  return *this;
}

} // namespace detail

} // namespace clockwork
