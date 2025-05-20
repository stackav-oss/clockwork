// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/dial/alignment_type.hh"
#include "clockwork/dial/approx_aligner_config.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/time/sync_time.hh"

#include <array>
#include <optional>
#include <sys/types.h>
#include <tuple>
#include <vector>

namespace clockwork
{

/// The alignment policy struct provides the static functions used by the aligner to determine the
/// alignment of a given set of inputs. While these are fully customizable there are pre-built configurations
/// provided in `clockwork/dial/approx_aligner_policies.hh` to cover the most common use
/// cases. It is recommended to use one of the provided implemenations unless they do not cover your specific
/// use case.
///
/// @tparam InputPolicy The policy struct used to describe an input.
///   struct InputPolicy
///   {
///     /// Underlying message type.
///     using MsgType = ...;
///     /// Type used for the extracted value from message.
///     using ValueType = ...;
///     /// The max size of the input view.
///     static constexpr auto max_msgs = ...;
///
///     /// Function used to extract the value used for alignment from the message.
///     static constexpr ValueType get_value(const MsgType& msg)
///     {
///       ...
///     }
///   };
///
/// @tparam PolicyType The policy to use for alignment.
///   template <typename... InputPolicies>
///   struct Policy
///   {
///     /// Type used for the extracted value from inputs.
///     using ValueType = ...;
///     using ValuePtrArray = std::array<const ValueType *, input_count>;
///     template <typename InputPolicy>
///     using InputType = MessageInputDialWithCursorControl<typename InputPolicy::MsgType, InputPolicy::max_msgs>;
///     using InputTuple = std::tuple<InputType<InputPolicies>&...>;
///     using IteratorTuple = std::tuple<typename InputType<InputPolicies>::IteratorType...>
///
///     /// Customizable validate inputs function.
///     /// @param[in] inputs Set of input begin/end iterators to validate.
///     /// @return True is the inputs are valid, false otherwise.
///     static constexpr bool validate_inputs(const InputTuple & inputs)
///     {
///       ...
///     }
///
///     /// Customizable less than operator for input sets.
///     /// @param[in] lhs The array of indices on the left hand side of the operator.
///     /// @param[in] rhs The array of indices on the right hand side of the operator.
///     /// @return true if the lhs set is less than the rhs set.
///     static constexpr bool less_than(const IndexArray &lhs, const IndexArray &rhs)
///     {
///       ...
///     }
///
///     /// Customizable alignment objective function to minimize.
///     /// @param[in] values Array of input values to evaluate.
///     /// @return Objective function score. Lower the value the better the alignment.
///     static constexpr ValueType objective(const std::array<const ValueType *, input_count> &values)
///     {
///       ...
///     }
///   };
template <template <typename...> typename PolicyType, typename... InputPolicies>
struct ApproxAligner
{
  static constexpr auto input_count = sizeof...(InputPolicies);
  using Config = Tappy<ApproxAlignerConfig>;
  using State = Tappy<ApproxAlignerState>;
  using Policy = PolicyType<InputPolicies...>;

  template <typename InputPolicy>
  using InputType = typename Policy::template InputType<InputPolicy>;
  using InputTuple = typename Policy::InputTuple;
  using InputItTuple = typename Policy::InputItTuple;

  using ValueType = typename Policy::ValueType;
  using ValueVector = std::pmr::vector<ValueType>;
  using ValueVectorsArray = std::array<ValueVector, input_count>;
  using ValuePtrArray = std::array<const ValueType*, input_count>;
  using IndexArray = std::array<ssize_t, input_count>;

  using SyncTime = jewels::time::SyncTime;

  /// Alignment data.
  struct Alignment
  {
    /// The objective function score for the alignment.
    ValueType score = {};

    /// The aligned input iterators.
    InputItTuple inputs = {};

    /// Equality operator.
    bool operator==(const Alignment&) const = default;
  };

  /// Result type from attempted alignment.
  struct Result
  {
    /// The time associated with this result.
    SyncTime time_of_validity = {};

    /// The type of alignment found.
    ApproxAlignerStateType state = ApproxAlignerStateType::unspecified;

    /// The type of alignment found.
    AlignmentType type = AlignmentType::none;

    /// Alignment data, only set if alignment type is full or partial.
    std::optional<Alignment> alignment = {};

    /// Equality operator.
    bool operator==(const Result&) const = default;
  };

  /// Find the next best aligned set from the input views.
  /// @param[in] resource The memory resource to use.
  /// @param[in] config The aligner runtime configuration.
  /// @param[in] state The aligner state.
  /// @param[in] now The current time.
  /// @param[in] inputs The set of iterators per input.
  static constexpr Result find_alignment(
    jewels::memory::MemoryResource resource,
    const Config& config,
    const State& state,
    const InputTuple& inputs,
    SyncTime now);

  /// Accept the aligned set and update the state.
  /// @param[in,out] state The aligner state to update.
  /// @param[in,out] inputs The inputs to update.
  /// @param[in] result The result from the alignment.
  static constexpr void commit(State& state, InputTuple& inputs, const Result& result);

  /// Reset the state allowing all current messages to be considered for alignment
  /// @param[in,out] state The aligner state to update.
  /// @param[in,out] inputs The inputs to update.
  /// @param[in] now The current time.
  static constexpr void reset(State& state, InputTuple& inputs, SyncTime now);

  /// Reset the state and exclude all messages up to the provided iterators per input.
  /// @param[in,out] state The aligner state to update.
  /// @param[in,out] inputs The inputs to update.
  /// @param[in] now The current time.
  /// @param[in] new_begins The set of iterators to use for the input begins.
  static constexpr void clear(State& state, InputTuple& inputs, SyncTime now, const InputItTuple& new_begins);

  /// Extract all input values.
  /// @note Helper function that is not intended to be called directly.
  /// @params[in] resource The memory resource to use.
  /// @params[in] inputs The input ranges.
  /// @return Array of vectors of the extracted values from each input, in order.
  static constexpr ValueVectorsArray extract_values(jewels::memory::MemoryResource resource, const InputTuple& inputs);

  /// Find the next full alignment, if any.
  /// @note Helper function that is not intended to be called directly, use `find_alignment` instead.
  /// @param[in] config The aligner configuration.
  /// @param[in] inputs The tuple of dial inputs.
  /// @param[in] values_array The array of extracted values from the inputs.
  /// @return Tuple of optional alignments, first is full alignment that satisfies all the requirements, the second is
  /// the
  ///  best alignment that does not satisfy the requirements, if any.
  static constexpr std::tuple<std::optional<Alignment>, std::optional<Alignment>>
  find_full_alignment(const Config& config, const InputTuple& inputs, const ValueVectorsArray& values_array);
};

namespace detail
{
/// Helper class to generate all combinations of the input values.
template <typename ValueType, size_t input_count>
class CombinationGenerator
{
  static_assert(input_count > 0, "CombinationGenerator input size must be greater than zero.");

public:
  using ValueVector = std::pmr::vector<ValueType>;
  using ValueVectorIt = typename ValueVector::const_iterator;
  using ValueVectorItArray = std::array<ValueVectorIt, input_count>;
  using ValueVectorsArray = std::array<ValueVector, input_count>;
  using ValuePtrArray = std::array<const ValueType*, input_count>;
  using IndexArray = std::array<ssize_t, input_count>;

  class Iterator
  {
  public:
    Iterator(const CombinationGenerator* generator, bool done);
    [[nodiscard]] bool operator==(const Iterator& rhs) const;
    [[nodiscard]] bool operator!=(const Iterator& rhs) const;
    [[nodiscard]] const ValuePtrArray& value_ptrs() const;
    [[nodiscard]] IndexArray indices() const;
    Iterator& operator++();

  private:
    const CombinationGenerator* generator_;
    bool done_;
    ValueVectorItArray iterators_;
    ValuePtrArray value_ptrs_;
  };

  /// Constructor.
  /// @param[in] Pointer to the inputs values array.
  explicit CombinationGenerator(jewels::memory::ObjectPtr<const ValueVectorsArray> inputs);

  /// Get the begin iterator.
  /// @return The begin iterator.
  Iterator begin();

  /// Get the end iterator.
  /// @return The end iterator.
  Iterator end();

private:
  /// The original input vectors.
  jewels::memory::ObjectPtr<const ValueVectorsArray> inputs_;
  /// Flag indicating all combinations have been generated.
  bool done_ = {};
};
} // namespace detail

} // namespace clockwork

#include "clockwork/dial/approx_aligner.inl"
