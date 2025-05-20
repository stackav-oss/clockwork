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

template <class MsgViewType, size_t max_size>
constexpr MessageInputDial<MsgViewType, max_size>::MessageInputDial(
  ViewType buffer_view, IteratorType cursor, IteratorType first_new) noexcept
  : buffer_view_(std::move(buffer_view)), cursor_(std::move(cursor)), first_new_(std::move(first_new))
{
}

template <class MsgViewType, size_t max_size>
constexpr auto MessageInputDial<MsgViewType, max_size>::get_view() const noexcept -> const ViewType&
{
  return buffer_view_;
}

template <class MsgViewType, size_t max_size>
constexpr auto MessageInputDial<MsgViewType, max_size>::get_cursor() const noexcept -> IteratorType
{
  return cursor_;
}

template <class MsgViewType, size_t max_size>
constexpr auto MessageInputDial<MsgViewType, max_size>::get_cursor_view() const noexcept
{
  return std::ranges::subrange<IteratorType>(get_cursor(), end());
}

template <class MsgViewType, size_t max_size>
constexpr auto MessageInputDial<MsgViewType, max_size>::get_new_msgs_view() const noexcept
{
  return std::ranges::subrange<IteratorType>(get_first_new(), end());
}

template <class MsgViewType, size_t max_size>
constexpr auto MessageInputDial<MsgViewType, max_size>::get_first_new() const noexcept -> IteratorType
{
  return first_new_;
}

template <class MsgViewType, size_t max_size>
constexpr auto MessageInputDial<MsgViewType, max_size>::end() const noexcept -> IteratorType
{
  return buffer_view_.end();
}

template <class MsgViewType, size_t max_size>
constexpr void MessageInputDial<MsgViewType, max_size>::set_cursor(IteratorType cursor) noexcept
{
  cursor_ = cursor;
}

} // namespace clockwork
