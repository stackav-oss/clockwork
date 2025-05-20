// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/cog/input_condition.hh"
#include "clockwork/common/process_description.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"

#include <array>
#include <cstddef>
#include <iterator>
#include <memory>
#include <tuple>

namespace clockwork
{

/// Helper class to handle the set of cog subscribers. Maintains the necessary bookkeeping and
/// conversion to dial input types based on the templated policies.
///
/// @tparam Policies see InputCondition::Policy
template <typename... Policies>
class CogConditions
{
public:
  static constexpr auto policy_count = sizeof...(Policies);
  using PoliciesTuple = std::tuple<Policies...>;
  template <typename Policy>
  using InputConditionType = std::unique_ptr<InputCondition<Policy>>;
  using InputConditionsTuple = std::tuple<InputConditionType<Policies>...>;
  using ConditionsTuple = std::tuple<typename InputCondition<Policies>::ConditionType...>;
  using LastConsumedTuple = std::tuple<jewels::Uuid<common::EndpointClassId>, pinion::BufferIterator>;
  using PinionDifferenceType = typename std::iterator_traits<pinion::BufferIterator>::difference_type;

  /// Construct from a pinion subscriber handle.
  explicit CogConditions(jewels::memory::MemoryResource resource) noexcept;

  /// Validate that all internal types are set.
  [[nodiscard]] bool validate() const;

  /// Set the subscriber handle.
  /// @tparam CogType The cog type setting the handle, it is expected to have a `notify()` call to be invoked on
  /// updates.
  [[nodiscard]] jewels::expected<void, jewels::MonoError>
  set_handle(jewels::Uuid<common::EndpointClassId> endpoint_id, pinion::SubscriberHandle handle);

  /// Construct the ConditionTypes for the subscribers.
  /// @return tuple with all subscriber conditions
  [[nodiscard]] ConditionsTuple make_conditions() const;

  /// Update the last consumed value with the saved iterator from make input.
  /// @param[in] endpoint_id The subscriber endpoint associated with this iteratro
  /// @param[in] las_consumed The iterator pointing to the last consumed
  void commit(jewels::Uuid<common::EndpointClassId> endpoint_id, pinion::BufferIterator last_consumed);

  /// Update the last consumed value with the saved iterator from make input.
  /// @param[in] records Arrray of last consumed iterators records.
  template <std::size_t input_count>
  void commit(const std::array<LastConsumedTuple, input_count>& records);

  /// Create the of endpoint id and max new messsages based on the conditions.
  /// @tparam ConditionPolicies The set of condition policies.
  /// @param[in] conditions The conditions tuple
  /// @return Tuple of endpoint id and max new messages count.
  template <typename ViewPolicy>
  static PinionDifferenceType get_max_new_msgs(const ConditionsTuple& conditions);

private:
  /// Memory resource
  jewels::memory::MemoryResource resource_;
  /// Policy structs
  PoliciesTuple policies_;
  /// The conditions
  InputConditionsTuple conditions_;
};

} // namespace clockwork

#include "clockwork/cog/cog_conditions.inl"
