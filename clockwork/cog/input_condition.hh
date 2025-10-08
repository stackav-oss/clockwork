// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/dial/cond_messages_present.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "jewels/std/expected.hh"

#include <wise_enum.h>

#include <cstdint>
#include <optional>
#include <ranges>
namespace clockwork
{

WISE_ENUM_CLASS((InputConditionType, uint8_t), any_message, new_message)

/// Helper class to handle a single input condition. Maintains the necessary bookkeeping and conversion to dial
/// condition type from the templated policy.
///
/// @tparam Policy structure as follows:
///   struct Policy
///   {
///     // The endpoint id of the subscriber
///     static constexpr EndpointClassId endpoint_id;
///     // The minimum bound on inputs when triggering.
///     static constexpr size_t bounds_min;
///     // The maximum bound on inputs when triggering.
///     static constexpr size_t bounds_max;
///     // The input condition, every vs latest message.
///     static constexpr InputConditionType condition_type;
///   };
template <typename PolicyT>
class InputCondition
{
public:
  using Policy = PolicyT;
  static constexpr auto endpoint_id = Policy::endpoint_id;
  static constexpr auto bounds_min = Policy::bounds_min;
  static constexpr auto bounds_max = Policy::bounds_max;
  static constexpr auto condition_type = Policy::condition_type;

  using ConditionType = MessagePresentCondition<bounds_min, bounds_max>;

  /// Construct from a pinion subscriber handle.
  /// @param subscriber The subscriber handle
  explicit InputCondition(pinion::SubscriberHandle subscriber) noexcept;

  /// Default constructor for non-connected endpoints
  /// Creates an InputCondition without a subscriber handle
  InputCondition() noexcept;
  /// Validate that all internal types are set correctly.
  [[nodiscard]] bool validate() const;

  /// Construct the MessagesPresentCondition for this subscriber based on the input policies.
  /// @return Messages present condition.
  [[nodiscard]] ConditionType make_condition();

  /// Update the last viewed value with the saved iterator from make input.
  /// @param[in] last_viewed The iterator of the last viewed message on this input.
  void commit(pinion::BufferIterator last_viewed);

private:
  /// The underlying subscriber handle.
  std::optional<pinion::SubscriberHandle> subscriber_;
  /// Iterator for the last viewed message.
  pinion::BufferIterator last_viewed_;

  /// Get the available range based on the policy.
  [[nodiscard]] jewels::expected<std::ranges::subrange<pinion::BufferIterator>, pinion::ProgressError>
  available_range() const;
};

} // namespace clockwork

#include "clockwork/cog/input_condition.inl"
