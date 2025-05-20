// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/cog/input_view.hh"
#include "clockwork/common/process_description.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/observer.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"

#include <array>
#include <memory>
#include <tuple>

namespace clockwork
{

/// Helper class to handle the set of cog subscribers. Maintains the necessary bookkeeping and
/// conversion to dial input types based on the templated policies.
///
/// @tparam Policies see InputView::Policy
template <typename... Policies>
class CogInputs
{
public:
  static constexpr auto policy_count = sizeof...(Policies);
  using PoliciesTuple = std::tuple<Policies...>;
  template <typename Policy>
  using SubscriberType = std::shared_ptr<InputView<Policy>>;
  using SubscribersTuple = std::tuple<SubscriberType<Policies>...>;
  using InputDialTuple = std::tuple<typename InputView<Policies>::InputDialType...>;
  using LastViewedTuple = std::tuple<jewels::Uuid<common::EndpointClassId>, pinion::BufferIterator>;
  using LastViewedArray = std::array<LastViewedTuple, policy_count>;

  /// Construct from a pinion subscriber handle.
  explicit CogInputs(jewels::memory::MemoryResource resource) noexcept;

  /// Validate that all internal types are set.
  [[nodiscard]] bool validate() const;

  /// Set the subscriber handle.
  /// @tparam CogType The cog type setting the handle, it is expected to have a `notify()` call to be invoked on
  /// updates.
  template <typename CogType>
  [[nodiscard]] jewels::expected<std::shared_ptr<pinion::Observer>, jewels::MonoError> set_handle(
    jewels::Uuid<common::EndpointClassId> endpoint_id,
    pinion::SubscriberHandle handle,
    jewels::memory::ObjectPtr<CogType> cog);

  /// Construct the InputDial views for this set of inputs.
  /// @note The last consumed message will not change until `commit` is called. At which point the end
  /// iterator used during this call will replace the current last used iterator.
  /// @note While this function will not change the last consumed value used to generate the inputs, subsequent calls to
  /// this function, without calling `commit`, can still return different values as the underlying
  /// subscriber may have changed.
  /// @param[in] conditions The set of conditions associated with the cog inputs.
  /// @return Message dial inputs or unexpected if there was an error.
  template <typename ConditionsType>
  [[nodiscard]] jewels::expected<InputDialTuple, pinion::ProgressError>
  make_dial_inputs(const typename ConditionsType::ConditionsTuple& conditions);

  /// Update the last viewed and input cursors for all the inputs.
  /// @pre The inputs tuple is a reference to the input returned from the last call to `make_dial_input()`.
  /// @return input The dial inputs. Used by manual cursor inputs to get the current input cursor.
  /// @return The tuple of last consumed iterators.
  [[nodiscard]] LastViewedArray commit(const InputDialTuple& inputs);

  /// Check for channel overruns.
  /// @return true if any of the saved dial inputs are no longer availabe.
  [[nodiscard]] bool is_overrun() const;

private:
  /// Memory resource
  jewels::memory::MemoryResource resource_;
  /// Policy structs
  PoliciesTuple policies_;
  /// The subscribers
  SubscribersTuple subscribers_;
};

} // namespace clockwork

#include "clockwork/cog/cog_inputs.inl"
