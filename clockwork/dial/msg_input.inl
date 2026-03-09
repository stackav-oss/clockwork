// IWYU pragma: private, include "clockwork/dial/msg_input.hh"
#pragma once

#include "clockwork/dial/msg_input.hh"

#include <cstddef>
#include <ranges>
#include <utility>

namespace clockwork
{

namespace detail
{

template <class T>
void MsgPolicy<T>::construct(storage_type& storage, const T* ptr)
{
  storage = ptr;
}

template <class T>
void MsgPolicy<T>::destruct(storage_type& storage)
{
  storage = nullptr;
}

template <class T>
auto MsgPolicy<T>::get(storage_type& storage) -> reference
{
  return *storage;
}

template <class T>
auto MsgPolicy<T>::get(const storage_type& storage) -> const_reference
{
  return *storage;
}

} // namespace detail

template <class MsgViewType, size_t max_size, size_t min_messages, size_t min_new_messages>
constexpr MessageInputDial<MsgViewType, max_size, min_messages, min_new_messages>::MessageInputDial(
  ViewType buffer_view, IteratorType cursor, IteratorType first_new, bool connected) noexcept
  : MessageInputDial<MsgViewType, max_size, min_messages, min_new_messages>::MessageInputDial(
      buffer_view, cursor, first_new, 0, connected)
{
}

template <class MsgViewType, size_t max_size, size_t min_messages, size_t min_new_messages>
constexpr MessageInputDial<MsgViewType, max_size, min_messages, min_new_messages>::MessageInputDial(
  ViewType buffer_view, IteratorType cursor, IteratorType first_new, size_t skip_count, bool connected) noexcept
  : buffer_view_(std::move(buffer_view)),
    cursor_(std::move(cursor)),
    first_new_(std::move(first_new)),
    skip_count_(skip_count),
    connected_(connected)
{
}

template <class MsgViewType, size_t max_size, size_t min_messages, size_t min_new_messages>
constexpr auto MessageInputDial<MsgViewType, max_size, min_messages, min_new_messages>::get_view() const noexcept
  -> const ViewType&
{
  return buffer_view_;
}

template <class MsgViewType, size_t max_size, size_t min_messages, size_t min_new_messages>
constexpr auto MessageInputDial<MsgViewType, max_size, min_messages, min_new_messages>::get_cursor() const noexcept
  -> IteratorType
{
  return cursor_;
}

template <class MsgViewType, size_t max_size, size_t min_messages, size_t min_new_messages>
constexpr auto MessageInputDial<MsgViewType, max_size, min_messages, min_new_messages>::get_cursor_view() const noexcept
{
  return std::ranges::subrange<IteratorType>(get_cursor(), end());
}

template <class MsgViewType, size_t max_size, size_t min_messages, size_t min_new_messages>
constexpr auto
MessageInputDial<MsgViewType, max_size, min_messages, min_new_messages>::get_new_msgs_view() const noexcept
{
  return std::ranges::subrange<IteratorType>(get_first_new(), end());
}

template <class MsgViewType, size_t max_size, size_t min_messages, size_t min_new_messages>
constexpr auto& MessageInputDial<MsgViewType, max_size, min_messages, min_new_messages>::get_latest_msg() const noexcept
  requires(min_messages > 0)
{
  return get_view().back();
}

template <class MsgViewType, size_t max_size, size_t min_messages, size_t min_new_messages>
constexpr auto&
MessageInputDial<MsgViewType, max_size, min_messages, min_new_messages>::get_latest_new_msg() const noexcept
  requires(min_new_messages > 0)
{
  return get_new_msgs_view().back();
}

template <class MsgViewType, size_t max_size, size_t min_messages, size_t min_new_messages>
constexpr auto
MessageInputDial<MsgViewType, max_size, min_messages, min_new_messages>::get_nonempty_view() const noexcept
  requires(min_messages > 0)
{
  return get_view();
}

template <class MsgViewType, size_t max_size, size_t min_messages, size_t min_new_messages>
constexpr auto
MessageInputDial<MsgViewType, max_size, min_messages, min_new_messages>::get_nonempty_new_msgs_view() const noexcept
  requires(min_new_messages > 0)
{
  return get_new_msgs_view();
}

template <class MsgViewType, size_t max_size, size_t min_messages, size_t min_new_messages>
constexpr auto MessageInputDial<MsgViewType, max_size, min_messages, min_new_messages>::get_first_new() const noexcept
  -> IteratorType
{
  return first_new_;
}

template <class MsgViewType, size_t max_size, size_t min_messages, size_t min_new_messages>
constexpr auto MessageInputDial<MsgViewType, max_size, min_messages, min_new_messages>::end() const noexcept
  -> IteratorType
{
  return buffer_view_.end();
}

template <class MsgViewType, size_t max_size, size_t min_messages, size_t min_new_messages>
constexpr auto
MessageInputDial<MsgViewType, max_size, min_messages, min_new_messages>::num_messages_skipped() const noexcept -> size_t
{
  return skip_count_;
}

template <class MsgViewType, size_t max_size, size_t min_messages, size_t min_new_messages>
constexpr void
MessageInputDial<MsgViewType, max_size, min_messages, min_new_messages>::set_cursor(IteratorType cursor) noexcept
{
  cursor_ = cursor;
}

template <class MsgViewType, size_t max_size, size_t min_messages, size_t min_new_messages>
constexpr bool MessageInputDial<MsgViewType, max_size, min_messages, min_new_messages>::connected() const noexcept
{
  return connected_;
}

} // namespace clockwork
