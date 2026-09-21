// IWYU pragma: private, include "clockwork/dial/msg_input.hh"
#pragma once

#include "clockwork/dial/msg_input.hh"

#include <cstddef>
#include <cstdint>
#include <ranges>
#include <utility>

namespace clockwork
{

template <
  class MsgViewType,
  size_t max_size,
  size_t min_messages,
  size_t min_new_messages,
  bool manual_cursor_v,
  bool expose_seqno_v,
  bool use_device_ptr_v>
constexpr MessageInputDial<
  MsgViewType,
  max_size,
  min_messages,
  min_new_messages,
  manual_cursor_v,
  expose_seqno_v,
  use_device_ptr_v>::
  MessageInputDial(ViewType buffer_view, IteratorType cursor, IteratorType first_new, bool connected) noexcept
  : buffer_view_(std::move(buffer_view)),
    cursor_(std::move(cursor)),
    first_new_(std::move(first_new)),
    connected_(connected)
{
}

template <
  class MsgViewType,
  size_t max_size,
  size_t min_messages,
  size_t min_new_messages,
  bool manual_cursor_v,
  bool expose_seqno_v,
  bool use_device_ptr_v>
constexpr MessageInputDial<
  MsgViewType,
  max_size,
  min_messages,
  min_new_messages,
  manual_cursor_v,
  expose_seqno_v,
  use_device_ptr_v>::
  MessageInputDial(
    ViewType buffer_view, IteratorType cursor, IteratorType first_new, size_t skip_count, bool connected) noexcept
  : buffer_view_(std::move(buffer_view)),
    cursor_(std::move(cursor)),
    first_new_(std::move(first_new)),
    skip_count_(skip_count),
    connected_(connected)
{
}

template <
  class MsgViewType,
  size_t max_size,
  size_t min_messages,
  size_t min_new_messages,
  bool manual_cursor_v,
  bool expose_seqno_v,
  bool use_device_ptr_v>
constexpr auto MessageInputDial<
  MsgViewType,
  max_size,
  min_messages,
  min_new_messages,
  manual_cursor_v,
  expose_seqno_v,
  use_device_ptr_v>::get_view() const noexcept -> const ViewType&
{
  return buffer_view_;
}

template <
  class MsgViewType,
  size_t max_size,
  size_t min_messages,
  size_t min_new_messages,
  bool manual_cursor_v,
  bool expose_seqno_v,
  bool use_device_ptr_v>
constexpr auto MessageInputDial<
  MsgViewType,
  max_size,
  min_messages,
  min_new_messages,
  manual_cursor_v,
  expose_seqno_v,
  use_device_ptr_v>::get_cursor() const noexcept -> IteratorType
{
  return cursor_;
}

template <
  class MsgViewType,
  size_t max_size,
  size_t min_messages,
  size_t min_new_messages,
  bool manual_cursor_v,
  bool expose_seqno_v,
  bool use_device_ptr_v>
constexpr auto MessageInputDial<
  MsgViewType,
  max_size,
  min_messages,
  min_new_messages,
  manual_cursor_v,
  expose_seqno_v,
  use_device_ptr_v>::get_cursor_view() const noexcept
{
  return std::ranges::subrange<IteratorType>(get_cursor(), end());
}

template <
  class MsgViewType,
  size_t max_size,
  size_t min_messages,
  size_t min_new_messages,
  bool manual_cursor_v,
  bool expose_seqno_v,
  bool use_device_ptr_v>
constexpr auto MessageInputDial<
  MsgViewType,
  max_size,
  min_messages,
  min_new_messages,
  manual_cursor_v,
  expose_seqno_v,
  use_device_ptr_v>::get_new_msgs_view() const noexcept
{
  return std::ranges::subrange<IteratorType>(get_first_new(), end());
}

template <
  class MsgViewType,
  size_t max_size,
  size_t min_messages,
  size_t min_new_messages,
  bool manual_cursor_v,
  bool expose_seqno_v,
  bool use_device_ptr_v>
constexpr auto& MessageInputDial<
  MsgViewType,
  max_size,
  min_messages,
  min_new_messages,
  manual_cursor_v,
  expose_seqno_v,
  use_device_ptr_v>::get_latest_msg() const noexcept
  requires(min_messages > 0)
{
  return get_view().back();
}

template <
  class MsgViewType,
  size_t max_size,
  size_t min_messages,
  size_t min_new_messages,
  bool manual_cursor_v,
  bool expose_seqno_v,
  bool use_device_ptr_v>
constexpr auto& MessageInputDial<
  MsgViewType,
  max_size,
  min_messages,
  min_new_messages,
  manual_cursor_v,
  expose_seqno_v,
  use_device_ptr_v>::get_latest_new_msg() const noexcept
  requires(min_new_messages > 0)
{
  return get_new_msgs_view().back();
}

template <
  class MsgViewType,
  size_t max_size,
  size_t min_messages,
  size_t min_new_messages,
  bool manual_cursor_v,
  bool expose_seqno_v,
  bool use_device_ptr_v>
constexpr auto MessageInputDial<
  MsgViewType,
  max_size,
  min_messages,
  min_new_messages,
  manual_cursor_v,
  expose_seqno_v,
  use_device_ptr_v>::get_nonempty_view() const noexcept
  requires(min_messages > 0)
{
  return get_view();
}

template <
  class MsgViewType,
  size_t max_size,
  size_t min_messages,
  size_t min_new_messages,
  bool manual_cursor_v,
  bool expose_seqno_v,
  bool use_device_ptr_v>
constexpr auto MessageInputDial<
  MsgViewType,
  max_size,
  min_messages,
  min_new_messages,
  manual_cursor_v,
  expose_seqno_v,
  use_device_ptr_v>::get_nonempty_new_msgs_view() const noexcept
  requires(min_new_messages > 0)
{
  return get_new_msgs_view();
}

template <
  class MsgViewType,
  size_t max_size,
  size_t min_messages,
  size_t min_new_messages,
  bool manual_cursor_v,
  bool expose_seqno_v,
  bool use_device_ptr_v>
constexpr auto MessageInputDial<
  MsgViewType,
  max_size,
  min_messages,
  min_new_messages,
  manual_cursor_v,
  expose_seqno_v,
  use_device_ptr_v>::get_first_new() const noexcept -> IteratorType
{
  return first_new_;
}

template <
  class MsgViewType,
  size_t max_size,
  size_t min_messages,
  size_t min_new_messages,
  bool manual_cursor_v,
  bool expose_seqno_v,
  bool use_device_ptr_v>
constexpr auto MessageInputDial<
  MsgViewType,
  max_size,
  min_messages,
  min_new_messages,
  manual_cursor_v,
  expose_seqno_v,
  use_device_ptr_v>::end() const noexcept -> IteratorType
{
  return buffer_view_.end();
}

template <
  class MsgViewType,
  size_t max_size,
  size_t min_messages,
  size_t min_new_messages,
  bool manual_cursor_v,
  bool expose_seqno_v,
  bool use_device_ptr_v>
constexpr auto MessageInputDial<
  MsgViewType,
  max_size,
  min_messages,
  min_new_messages,
  manual_cursor_v,
  expose_seqno_v,
  use_device_ptr_v>::num_messages_skipped() const noexcept -> size_t
{
  return skip_count_;
}

template <
  class MsgViewType,
  size_t max_size,
  size_t min_messages,
  size_t min_new_messages,
  bool manual_cursor_v,
  bool expose_seqno_v,
  bool use_device_ptr_v>
constexpr void MessageInputDial<
  MsgViewType,
  max_size,
  min_messages,
  min_new_messages,
  manual_cursor_v,
  expose_seqno_v,
  use_device_ptr_v>::set_cursor(IteratorType cursor) noexcept
  requires(manual_cursor)
{
  cursor_ = cursor;
}

template <
  class MsgViewType,
  size_t max_size,
  size_t min_messages,
  size_t min_new_messages,
  bool manual_cursor_v,
  bool expose_seqno_v,
  bool use_device_ptr_v>
constexpr uint64_t MessageInputDial<
  MsgViewType,
  max_size,
  min_messages,
  min_new_messages,
  manual_cursor_v,
  expose_seqno_v,
  use_device_ptr_v>::get_sequence_number(IteratorType iter) const noexcept
  requires(expose_seqno)
{
  return iter.seqno();
}

template <
  class MsgViewType,
  size_t max_size,
  size_t min_messages,
  size_t min_new_messages,
  bool manual_cursor_v,
  bool expose_seqno_v,
  bool use_device_ptr_v>
constexpr bool MessageInputDial<
  MsgViewType,
  max_size,
  min_messages,
  min_new_messages,
  manual_cursor_v,
  expose_seqno_v,
  use_device_ptr_v>::connected() const noexcept
{
  return connected_;
}

template <
  class MsgViewType,
  size_t max_size,
  size_t min_messages,
  size_t min_new_messages,
  bool manual_cursor_v,
  bool expose_seqno_v,
  bool use_device_ptr_v>
auto MessageInputDial<
  MsgViewType,
  max_size,
  min_messages,
  min_new_messages,
  manual_cursor_v,
  expose_seqno_v,
  use_device_ptr_v>::device_ptr(IteratorType iter) const noexcept -> MsgDevicePtr
  requires(use_device_ptr)
{
  return iter.device_ptr();
}

} // namespace clockwork
