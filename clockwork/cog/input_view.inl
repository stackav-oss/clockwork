// IWYU pragma: private, include "clockwork/cog/input_view.hh"
#pragma once
#include "clockwork/cog/input_view.hh"

#include "clockwork/cog/cog_statistics.hh"
#include "clockwork/dsl/cog/ten_nanosecond_type.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/slot_ref.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "jewels/container/compare.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/conversions.hh"
#include "jewels/time/sync_time.hh"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iterator>
#include <memory_resource>
#include <optional>
#include <ranges>
#include <span>
#include <sys/types.h>
#include <tuple>
#include <typeinfo>
#include <utility>
#include <vector>

namespace clockwork
{

template <typename Policy>
InputView<Policy>::InputView(pinion::SubscriberHandle subscriber, jewels::memory::MemoryResource resource) noexcept
  : InputView<Policy>::InputView(subscriber, false, resource)
{
}

template <typename Policy>
InputView<Policy>::InputView(
  pinion::SubscriberHandle subscriber,
  size_t metrics_batch_size,
  jewels::memory::MemoryResource resource,
  bool running_offline) noexcept
  : subscriber_(std::move(subscriber)),
    msg_view_buffer_{std::in_place, std::span(msg_view_storage_)},
    running_offline_(running_offline),
    input_metrics_{.event_metrics = std::pmr::vector<InputEventMetrics>(resource), .telemetry_metrics{}},
    skipped_safety_{false},
    skipped_count_{0UL},
    metrics_batch_size_(metrics_batch_size)
{
}

template <typename Policy>
InputView<Policy>::InputView(
  size_t metrics_batch_size, jewels::memory::MemoryResource resource, bool running_offline) noexcept
  : subscriber_(std::nullopt),
    msg_view_buffer_{std::in_place, std::span(msg_view_storage_)},
    running_offline_(running_offline),
    input_metrics_{.event_metrics = std::pmr::vector<InputEventMetrics>(resource), .telemetry_metrics{}},
    skipped_safety_{false},
    skipped_count_{0UL},
    metrics_batch_size_(metrics_batch_size)
{
}
template <typename Policy>
bool InputView<Policy>::validate() const
{
  if (!subscriber_)
  {
    return true;
  }
  const auto& layout = subscriber_->layout();
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
void InputView<Policy>::update_input_metrics(
  int new_msg_count, PinionDifferenceType num_dropped_messages, int64_t message_staleness)
{

  std::ignore = input_metrics_.telemetry_metrics.message_staleness.update(
    std::chrono::duration_cast<TenNanoseconds>(std::chrono::nanoseconds(message_staleness)));
  std::ignore = input_metrics_.telemetry_metrics.num_unseen_messages.update(static_cast<uint16_t>(new_msg_count));
  std::ignore = input_metrics_.telemetry_metrics.messages_dropped.update(static_cast<uint16_t>(num_dropped_messages));

  // Not necessarily an error because metrics collection might be disabled.
  if (input_metrics_.event_metrics.size() >= metrics_batch_size_)
  {
    return;
  }
  input_metrics_.event_metrics.emplace_back(
    InputEventMetrics{
      .num_unseen_messages = static_cast<uint16_t>(new_msg_count),
      .message_staleness = std::chrono::duration_cast<TenNanoseconds>(std::chrono::nanoseconds(message_staleness)),
      .messages_dropped = static_cast<uint16_t>(num_dropped_messages),

    });
}

template <typename Policy>
const AggregatedInputMetrics& InputView<Policy>::get_aggregated_input_metrics() const
{
  return input_metrics_;
}

template <typename Policy>
void InputView<Policy>::reset_metrics()
{
  input_metrics_.event_metrics.clear();
  input_metrics_.telemetry_metrics.message_staleness.clear();
  input_metrics_.telemetry_metrics.num_unseen_messages.clear();
  input_metrics_.telemetry_metrics.messages_dropped.clear();
}

template <typename Policy>
auto InputView<Policy>::make_dial_input(PinionDifferenceType max_new_msgs, jewels::time::SyncTime current_time)
  -> jewels::expected<InputDialType, pinion::ProgressError>
{
  // Populate the view buffer with the desired range. Copying the data if necessary.
  if (!subscriber_)
  {
    const ViewType view{msg_view_buffer_};
    return InputDialType(view, view.end(), view.end(), false);
  }
  auto available = subscriber_->available();

  // Determine the end of the view taking into account the max new messages to be added.

  auto bounded_last_viewed =
    (last_viewed_.is_sentinel() || last_viewed_ < available.begin()) ? available.begin() : last_viewed_;
  auto new_msg_count = std::distance(bounded_last_viewed, available.end());
  auto end = std::next(bounded_last_viewed, std::min(max_new_msgs, new_msg_count));

  auto num_dropped_messages = new_msg_count > Policy::max_view_size ? new_msg_count - Policy::max_view_size : 0;
  int64_t message_staleness = 0;
  if (new_msg_count != 0)
  {
    auto latest_message = std::prev(end);
    message_staleness = jewels::time::get_ns(current_time) - latest_message->header()->publish_timestamp;
  }
  if (available.begin() != available.end())
  {
    auto latest_message = std::prev(available.end());
    latest_message_time_ =
      jewels::time::SyncTime(std::chrono::nanoseconds(latest_message->header()->publish_timestamp));
  }

  update_input_metrics(static_cast<int>(new_msg_count), num_dropped_messages, message_staleness);

  // Determine the view range based on the views max msg count.

  auto bounded_available_size = std::distance(available.begin(), end);
  auto begin = std::prev(end, std::min<PinionDifferenceType>(max_view_size, bounded_available_size));

  // Skip to the latest three messages on the first execution.

  if (!running_offline_ && last_viewed_.is_sentinel())
  {
    end = available.end();
    begin = std::prev(end, std::min<PinionDifferenceType>(max_view_size, bounded_available_size));
  }

  // If this input is configured with a safety marign, check to see if we've
  // violated it. If so, skip forward to the most recent messages.

  skipped_safety_ = apply_safety_margin(available, begin, end);

  // If configured to preemptively skip, check to see if we've fallen far enough
  // behind to jump forward.
  auto num_skipped = apply_skip_threshold(available, begin, end);
  skipped_count_ = num_skipped.value_or(0UL);

  auto range = pinion::to_message_range<const MsgType>(std::ranges::subrange<pinion::SlotRef>(begin, end));
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
  if (input_cursor_.is_sentinel() || input_cursor_ < begin)
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
  if (last_viewed_.is_sentinel() || last_viewed_ < begin)
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

  if (num_skipped)
  {
    return InputDialType(view, input_cursor_it, first_new_it, *num_skipped);
  }

  return InputDialType(view, input_cursor_it, first_new_it);
}

template <typename Policy>
bool InputView<Policy>::apply_safety_margin(const auto& available, pinion::SlotRef& begin, pinion::SlotRef& end) const
{
  if constexpr (!safety_margin)
  {
    return false;
  }
  if (!subscriber_)
  {
    return false;
  }
  // Don't apply this policy on first exec or when running offline.
  if (last_viewed_.is_sentinel() || running_offline_)
  {
    return false;
  }

  // Haven't executed yet.
  if (begin.is_sentinel())
  {
    return false;
  }

  // The channel size isn't known when `Policy` is created, we only know it at
  // runtime. So, if the margin is set to -1, clk is requesting the default: we
  // should pick the greater of the view size and half the buffer size.
  auto adjusted_margin = static_cast<PinionDifferenceType>(
    *safety_margin == -1 ? std::max(subscriber_->layout().num_slots / 2U, static_cast<size_t>(max_view_size))
                         : static_cast<size_t>(*safety_margin));

  // We know the producer will never be further ahead of us than the buffer
  // size, unless we've been overrun(which would have caused us to terminate).
  // So, the distance from it to the beginning of this view is just the rest of
  // the buffer.

  auto distance =
    std::distance(available.end(), begin) + static_cast<PinionDifferenceType>(subscriber_->layout().num_slots);
  if (distance < adjusted_margin)
  {
    end = available.end();
    begin = std::prev(end, std::min<PinionDifferenceType>(max_view_size, std::distance(available.begin(), end)));
    return true;
  }
  return false;
}

template <typename Policy>
std::optional<size_t>
InputView<Policy>::apply_skip_threshold(const auto& available, pinion::SlotRef& begin, pinion::SlotRef& end) const
{
  if constexpr (!skip_threshold)
  {
    return std::nullopt;
  }

  // Don't apply this policy on first exec.
  if (last_viewed_.is_sentinel())
  {
    return {0U};
  }

  auto distance = std::distance(end, available.end());
  if (distance >= static_cast<PinionDifferenceType>(*skip_threshold))
  {
    // TODO(OI-3224): we should have a metric that tracks these skips, so we can analyze it offline.
    end = available.end();
    auto old_begin = begin;
    begin = std::prev(
      end,
      std::min<PinionDifferenceType>(
        max_view_size, std::min<PinionDifferenceType>(max_view_size, std::distance(available.begin(), end))));
    return {static_cast<size_t>(begin - old_begin)};
  }

  return {0U};
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

  if (saved_begin_.is_sentinel() || !subscriber_)
  {
    return false;
  }
  auto available = subscriber_->available();
  if (saved_begin_ < available.begin())
  {
    jewels::log_cerr_error(
      "Channel '{}' has been overrun. Cog is at {}, producer is at {}",
      Policy::name,
      saved_begin_.index(),
      available.end().index());
    return true;
  }
  return false;
}

template <typename Policy>
bool InputView<Policy>::almost_overrun() const
{
  if constexpr (copy_inputs)
  {
    return false;
  }

  if (saved_begin_.is_sentinel() || !subscriber_ || subscriber_->buffer().is_published_once())
  {
    return false;
  }

  auto available = subscriber_->available();
  if (saved_begin_ < available.begin())
  {
    // We've already been overrun.
    jewels::log_cerr_error(
      "Channel '{}' has been overrun. Cog is at {}, producer is at {}",
      Policy::name,
      saved_begin_.index(),
      available.end().index());
    return true;
  }

  // Consider the cog fatally behind if the producer is within ten percent of the
  // buffer size or two messages of the cog's view, whichever is greater.
  static constexpr double buffer_margin_factor{0.1};
  static constexpr PinionDifferenceType min_margin{2};
  const auto margin = std::max(
    static_cast<PinionDifferenceType>(static_cast<double>(subscriber_->layout().num_slots) * buffer_margin_factor),
    min_margin);

  // NOTE: We care about the distance from the producer's position to the
  // beginning of this view. So, we calculate the distance using the end of the
  // available range. Measuring from the beginning of the range can only tell us
  // if we've already been overrun, which we checked above.
  //
  // Given the overrun check above, we know the producer will never be further
  // ahead of us than the buffer size. So, the distance from it to the
  // beginning of this view is just the rest of the buffer.

  auto distance =
    std::distance(available.end(), saved_begin_) + static_cast<PinionDifferenceType>(subscriber_->layout().num_slots);
  if (distance < margin)
  {
    jewels::log_cerr_error(
      "Channel '{}' is dangerously close to being overrun. Producer at {} has exceeded margin of {} to cog at {}",
      Policy::name,
      available.end().index(),
      margin,
      saved_begin_.index());
    return true;
  }
  return false;
}

template <typename Policy>
bool InputView<Policy>::is_published_once_channel_invalid() const
{
  return subscriber_ && subscriber_->buffer().is_published_once() && subscriber_->buffer().get_publish_count() > 1U;
}

template <typename Policy>
jewels::time::SyncTime InputView<Policy>::latest_message_time() const
{
  return latest_message_time_;
}

template <typename Policy>
bool InputView<Policy>::did_safety_skip() const
{
  return skipped_safety_;
}

template <typename Policy>
size_t InputView<Policy>::last_skipped_count() const
{
  return skipped_count_;
}

} // namespace clockwork
