// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/common/process_description.hh"
#include "clockwork/dial/msg_input.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "jewels/container/circular_buffer.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"

#include <array>
#include <iterator>
#include <span>
#include <tuple>
#include <type_traits>

namespace clockwork
{

/// Helper class to handle cog message input views. Handles optional copying of inputs and checking for overruns.
///
/// @tparam Policy structure as follows:
///   struct Policy
///   {
///     // The input message type
///     using MsgType;
///     // The endpoint id of the subscriber
///     static constexpr EndpointClassId endpoint_id;
///     // The maximum view size for the dial input.
///     static constexpr size_t max_view_size;
///     // Flag indicating if the inputs should be copied.
///     static constexpr auto copy_inputs = false;
///     // Flag indicating if the cursor is manually controlled.
///     static constexpr auto manual_cursor = false;
///   };
template <typename PolicyT>
class InputView
{
public:
  using Policy = PolicyT;
  using MsgType = typename Policy::MsgType;
  using PinionDifferenceType = typename std::iterator_traits<pinion::BufferIterator>::difference_type;
  static constexpr auto endpoint_id = Policy::endpoint_id;
  static constexpr auto max_view_size = Policy::max_view_size;
  static constexpr auto copy_inputs = Policy::copy_inputs;
  static constexpr auto manual_cursor = Policy::manual_cursor;

  using InputDialType = std::conditional_t<
    manual_cursor,
    MessageInputDialWithCursorControl<MsgType, max_view_size>,
    MessageInputDial<MsgType, max_view_size>>;

  using ViewType = typename MessageInputDial<MsgType, max_view_size>::ViewType;
  using ViewIteratorType = typename MessageInputDial<MsgType, max_view_size>::IteratorType;
  using LastViewedTuple = std::tuple<jewels::Uuid<common::EndpointClassId>, pinion::BufferIterator>;

  /// Construct from a pinion subscriber handle.
  /// @param subscriber The subscriber handle
  explicit InputView(pinion::SubscriberHandle subscriber) noexcept;

  /// Validate that all internal types are set correctly.
  [[nodiscard]] bool validate() const;

  /// Construct the MessageInputDial for this subscriber.
  /// @note The cursor and last viewed message will not change until `commit` is called. At which point the end
  /// iterator used during this call will replace the current last used iterator.
  /// @note While this function will not change the last viewed value used to generate the inputs, subsequent calls to
  /// this function, without calling `commit`, can still return different values as the underlying
  /// subscriber may have changed.
  /// @param[in] max_new_msgs The maximum number of new messages to add to the input view. This value is determine from
  /// the
  ///   minimum of max_bounds of all the message conditions on this input.
  /// @return Message dial input or unexpected if the conditions are not met.
  [[nodiscard]] jewels::expected<InputDialType, pinion::ProgressError>
  make_dial_input(PinionDifferenceType max_new_msgs);

  /// Update the last viewed value with the saved iterator from make input.
  /// @pre The input is a reference to the input returned from the last call to `make_dial_input()`.
  /// @return input The dial input. Used by manual cursor inputs to get the current input cursor.
  /// @return the last viewed tuple.
  [[nodiscard]] LastViewedTuple commit(const InputDialType& input);

  /// Check for channel overruns.
  /// @return true if any of the saved dial inputs are no longer availabe.
  [[nodiscard]] bool is_overrun() const;

private:
  using MessageInputCircularBuffer =
    jewels::container::CircularBuffer<detail::MsgPolicy<MsgType>, std::span<const MsgType*, max_view_size>>;
  using CopyStorageType = std::conditional_t<copy_inputs, std::array<MsgType, max_view_size>, std::array<MsgType, 0>>;

  /// The underlying subscriber handle.
  pinion::SubscriberHandle subscriber_;
  /// Iterator tracking the input cursor.
  pinion::BufferIterator input_cursor_;
  /// Iterator for the last viewed message.
  pinion::BufferIterator last_viewed_;
  /// Iterator for the last begin iterator (used for overrun checks)
  pinion::BufferIterator saved_begin_;
  /// Iterator for the last end iterator (used for keeping track of new messages)
  pinion::BufferIterator saved_end_;
  /// Storage for the message view buffer, used to construct the dial inputs.
  std::array<const MsgType*, max_view_size> msg_view_storage_;
  /// The circular message view buffer, used to construct the dial inputs.
  MessageInputCircularBuffer msg_view_buffer_;
  /// Storage for messages when copied inputs are required.
  CopyStorageType copy_inputs_storage_;

  /// Construct the MessageInputDial for the given range.
  [[nodiscard]] jewels::expected<InputDialType, pinion::ProgressError>
  make_dial_input_from_range(pinion::BufferIterator begin, pinion::BufferIterator end);
};

} // namespace clockwork

#include "clockwork/cog/input_view.inl"
