// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/dial/msg_input.hh"

#include <array>
#include <cstdint>
#include <sys/types.h>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace clockwork
{

/// Common definition for an ApproxAligner input using time of validity.
/// @tparam DialInputType The message dial input type.
template <typename DialInputType>
struct TovNanosecondsApproxAlignerInput
{
  using MsgType = typename DialInputType::MsgType;
  static constexpr auto max_msgs = DialInputType::max_msgs;
  static constexpr auto min_msgs = DialInputType::min_msgs;
  static constexpr auto min_new_msgs = DialInputType::min_new_msgs;
  using ValueType = int64_t;

  static constexpr ValueType get_value(const MsgType& msg)
  {
    return msg.get_time_of_validity().time_since_epoch().count();
  }
};

/// Common definition for an ApproxAligner input using time of validity field with _ns suffix.
/// @tparam DialInputType The message dial input type.
template <typename DialInputType>
struct TovNanosecondsNsSuffixApproxAlignerInput
{
  using MsgType = typename DialInputType::MsgType;
  static constexpr auto max_msgs = DialInputType::max_msgs;
  static constexpr auto min_msgs = DialInputType::min_msgs;
  static constexpr auto min_new_msgs = DialInputType::min_new_msgs;
  using ValueType = int64_t;

  static constexpr ValueType get_value(const MsgType& msg)
  {
    return msg.get_time_of_validity_ns().time_since_epoch().count();
  }
};

/// Helper to check that a given type is the same for a variadic template.
template <typename T, typename... Ts>
inline constexpr bool all_same_v = std::conjunction_v<std::is_same<T, Ts>...>;

/// File containing default/shared policies for use with the ApproxAligner class.
template <typename... InputPolicies>
struct ApproxAlignerPolicies
{
  static_assert(all_same_v<typename InputPolicies::ValueType...>, "All input value types must be the same.");

  static constexpr auto input_count = sizeof...(InputPolicies);
  static_assert(input_count > 1UL, "Stream alignment requires at least two input streams.");
  using ValueType = typename std::tuple_element_t<0, std::tuple<InputPolicies...>>::ValueType;
  using ValuePtrsArray = std::array<const ValueType*, input_count>;
  template <typename InputPolicy>
  using InputType = MessageInputDial<
    typename InputPolicy::MsgType,
    InputPolicy::max_msgs,
    InputPolicy::min_msgs,
    InputPolicy::min_new_msgs,
    true>;
  using InputTuple = std::tuple<InputType<InputPolicies>&...>;
  using InputItTuple = std::tuple<typename InputType<InputPolicies>::IteratorType...>;
  using IndexArray = std::array<ssize_t, input_count>;

  /// Always return true.
  /// @param[in] inputs Set of input begin/end iterators to validate.
  /// @return True.
  static constexpr bool always_valid_inputs(const InputTuple& inputs);

  /// Check that inputs are monotonically increasing.
  /// @param[in] inputs Set of input begin/end iterators to validate.
  /// @return True if all inputs are monotonically increasing.
  static constexpr bool monotonically_increasing_inputs(const InputTuple& inputs);

  /// Less than operator that uses the default array less than operator.
  /// @param[in] lhs The array of indices on the left hand side of the operator.
  /// @param[in] rhs The array of indices on the right hand side of the operator.
  /// @return true if the lhs set is less than the rhs set.
  static constexpr bool array_less_than(const IndexArray& lhs, const IndexArray& rhs);

  /// Compute exact alignment score. Where the score ranges from [0 - all aligned, input_count - no inputs].
  /// The lower the score the better the alignment.
  /// @param[in] values The input values array.
  /// @return Exact alignment score [0 - all aligned, input_count - no inputs].
  static constexpr ValueType exact_alignment_objective(const ValuePtrsArray& values);

  /// Compute approx alignment score based on input variance.
  /// The lower the score the better the alignment.
  /// @param[in] values The input values array.
  /// @return Variance of input values.
  static constexpr ValueType variance_objective(const ValuePtrsArray& values);

  /// Compute the approx alignment score based on the follower objective.
  /// The lower the score (further left on the number line) the better the alignment.
  /// Specifically, the objective is the first input minus the smallest of the remaining inputs.
  /// In this way, if the first input is the smallest, the score will be negative, otherwise
  /// the score will be positive.
  /// @note if the first input is nullptr, or if all other inputs are nullptr, the maximum score is returned.
  /// Otherwise, the subset of nullptr inputs are ignored for the purpose of finding the minimum.
  /// @param[in] values The input values array.
  /// @return Follower objective score.
  static constexpr ValueType follower_objective(const ValuePtrsArray& values);
};

} // namespace clockwork

#include "clockwork/dial/approx_aligner_policies.inl"
