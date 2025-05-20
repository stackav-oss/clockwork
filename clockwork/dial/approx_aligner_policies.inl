// IWYU pragma: private, include "clockwork/dial/approx_aligner_policies.hh"
#pragma once

#include "clockwork/dial/approx_aligner_policies.hh"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <limits>
#include <tuple>

namespace clockwork
{

template <typename... InputPolicies>
constexpr bool ApproxAlignerPolicies<InputPolicies...>::always_valid_inputs(const InputTuple& /*inputs*/)
{
  return true;
}

template <typename... InputPolicies>
constexpr bool ApproxAlignerPolicies<InputPolicies...>::monotonically_increasing_inputs(const InputTuple& inputs)
{
  auto is_monotonically_increasing = []<typename InputPolicy>(const auto& input)
  {
    if (input.get_cursor_view().size() < 2)
    {
      return true;
    }

    for (auto it0 = input.get_cursor(), it1 = std::next(it0); it1 != input.end(); ++it0, ++it1)
    {
      if (InputPolicy::get_value(*it0) >= InputPolicy::get_value(*it1))
      {
        return false;
      }
    }

    return true;
  };

  return std::apply(
    [&is_monotonically_increasing](auto&... input)
    { return (is_monotonically_increasing.template operator()<InputPolicies>(input) && ...); },
    inputs);
}

template <typename... InputPolicies>
constexpr bool ApproxAlignerPolicies<InputPolicies...>::array_less_than(const IndexArray& lhs, const IndexArray& rhs)
{
  return lhs < rhs;
}

template <typename... InputPolicies>
constexpr auto ApproxAlignerPolicies<InputPolicies...>::exact_alignment_objective(const ValuePtrsArray& values)
  -> ValueType
{
  auto equals = std::array<ValueType, input_count>{};

  for (size_t i = 0; i < values.size(); ++i)
  {
    if (values.at(i) == nullptr)
    {
      continue;
    }

    ++equals.at(i);

    for (size_t j = i + 1; j < values.size(); ++j)
    {
      if (values.at(j) == nullptr)
      {
        continue;
      }

      if (*values.at(i) == *values.at(j))
      {
        ++equals.at(i);
      }
    }
  }
  return static_cast<ValueType>(input_count) - *std::max_element(equals.begin(), equals.end());
}

template <typename... InputPolicies>
constexpr auto ApproxAlignerPolicies<InputPolicies...>::variance_objective(const ValuePtrsArray& values) -> ValueType
{
  auto sum = ValueType{0};
  auto count = ValueType{0};

  for (size_t i = 0; i < input_count; ++i)
  {
    if (values.at(i) != nullptr)
    {
      sum += *values.at(i);
      ++count;
    }
  }

  if (count == ValueType{0})
  {
    return std::numeric_limits<ValueType>::max();
  }

  auto mean = sum / count;

  auto sum_sqr_diff = ValueType{0};
  for (size_t i = 0; i < input_count; ++i)
  {
    if (values.at(i) != nullptr)
    {
      sum_sqr_diff += ((*values.at(i) - mean) * (*values.at(i) - mean));
    }
  }

  auto variance = sum_sqr_diff / count;
  return variance;
}

} // namespace clockwork
