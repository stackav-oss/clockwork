// IWYU pragma: private, include "clockwork/cog/input_view.hh"
#pragma once

#include "clockwork/cog/input_view.hh"

#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "jewels/container/compare.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/std/expected.hh"

#include <boost/iterator/iterator_facade.hpp>

#include <algorithm>
#include <iterator>
#include <ranges>
#include <span>
#include <sys/types.h>
#include <tuple>
#include <typeinfo>
#include <utility>

namespace clockwork
{

template <typename Policy>
InputView<Policy>::InputView(pinion::SubscriberHandle subscriber) noexcept
  : subscriber_(std::move(subscriber)), msg_view_buffer_{std::in_place, std::span(msg_view_storage_)}
{
}

template <typename Policy>
bool InputView<Policy>::validate() const
{
  const auto& layout = subscriber_.layout();
  const auto expected_msg_size = sizeof(typename Policy::MsgType);
  const auto configured_msg_size = layout.message_size;
  if (expected_msg_size != configured_msg_size)
  {
    jewels::log_cerr_error(
      "Subscriber '{}' for type '{}'  has incorrect message size: got {}, expected {}",
      Policy::name,
      typeid(Policy).name(),
      configured_msg_size,
      expected_msg_size);
    return false;
  }
  const auto& channel_size = layout.num_slots;
  if (channel_size < max_view_size)
  {
    jewels::log_cerr_warn("'{}' buffer {} < view {} size.", Policy::name, max_view_size, channel_size);
  }
  return true;
}

template <typename Policy>
auto InputView<Policy>::make_dial_input(PinionDifferenceType max_new_msgs)
  -> jewels::expected<InputDialType, pinion::ProgressError>
{
  // Populate the view buffer with the desired range. Copying the data if necessary.

  auto available = subscriber_.available();

  // Determine the end of the view taking into account the max new messages to be added.

  auto bounded_last_viewed =
    (is_sentinel_iterator(last_viewed_) || last_viewed_ < available.begin()) ? available.begin() : last_viewed_;
  auto new_msg_count = std::distance(bounded_last_viewed, available.end());
  auto end = std::next(bounded_last_viewed, std::min(max_new_msgs, new_msg_count));

  // Determine the view range based on the views max msg count.

  auto bounded_available_size = std::distance(available.begin(), end);
  auto begin = std::prev(end, std::min<PinionDifferenceType>(max_view_size, bounded_available_size));

  auto range = pinion::to_message_range<const MsgType>(std::ranges::subrange<pinion::BufferIterator>(begin, end));
  if (!range)
  {
    return jewels::unexpected{pinion::ProgressError{}};
  }

  msg_view_buffer_.clear();

  if constexpr (copy_inputs)
  {
    size_t index = 0;
    for (const auto& msg : *range)
    {
      auto& copy = copy_inputs_storage_.at(index);
      copy = msg;
      msg_view_buffer_.emplace_back(&copy);
      ++index;
    }
  }
  else
  {
    for (const auto& msg : *range)
    {
      msg_view_buffer_.emplace_back(&msg);
    }
  }

  const ViewType view{msg_view_buffer_};

  // Ensure the cursor is in the view.

  ViewIteratorType input_cursor_it;
  if (input_cursor_ < begin)
  {
    input_cursor_it = view.begin();
  }
  else if (input_cursor_ > end)
  {
    input_cursor_it = view.end();
  }
  else
  {
    input_cursor_it = std::next(view.begin(), std::distance(begin, input_cursor_));
  }

  // Determine the first new message iterator

  ViewIteratorType first_new_it;
  if (last_viewed_ < begin)
  {
    first_new_it = view.begin();
  }
  else if (last_viewed_ > end)
  {
    first_new_it = view.end();
  }
  else
  {
    first_new_it = std::next(view.begin(), std::distance(begin, last_viewed_));
  }

  // Store the end iterator as the temporary last viewed value. This will be the value
  // we update the last_viewed_ iterator to when `commit()` is called.

  saved_begin_ = begin;
  saved_end_ = end;

  // Create the message dial input

  return InputDialType(view, input_cursor_it, first_new_it);
}

template <typename Policy>
auto InputView<Policy>::commit(const InputDialType& input) -> LastViewedTuple
{
  // Update the cursor input based on the policy.

  if constexpr (Policy::manual_cursor)
  {
    input_cursor_ = std::next(saved_begin_, std::distance(input.get_view().begin(), input.get_cursor()));
  }
  else
  {
    std::ignore = input;
    input_cursor_ = saved_end_;
  }

  // Update the last viewed iterator to the end, everything on the view is considered "seen".

  last_viewed_ = saved_end_;

  // Reset saved state.

  saved_begin_ = {};
  saved_end_ = {};

  return {endpoint_id, last_viewed_};
}

template <typename Policy>
bool InputView<Policy>::is_overrun() const
{
  if constexpr (copy_inputs)
  {
    return false;
  }

  if (is_sentinel_iterator(saved_begin_))
  {
    return false;
  }

  return saved_begin_ < subscriber_.available().begin();
}

} // namespace clockwork
