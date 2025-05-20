// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/container/circular_buffer.hh"

#include <cstddef>
#include <iterator>
#include <ranges>
#include <span>

namespace clockwork
{

namespace detail
{

template <class T>
struct MsgPolicy
{
  using storage_type = const T*;
  using value_type = const T;
  using reference = const T&;
  using const_reference = const T&;

  /// Construct an object of type T* in the storage.
  /// @param storage Where to construct the T*.
  static void construct(storage_type& storage, const T* ptr);

  /// Destruct the T being held in the storage.
  ///
  /// Technically since this is just a pointer there's no destructor, but this
  /// sets the T* to nullptr to rapidly surface bugs with accessing a removed
  /// member.
  ///
  /// @param storage Where the T* is stored.
  static void destruct(storage_type& storage);

  /// Marshall an aligned storage as a reference.
  ///
  /// In our case we just dereference the pointer.  This does not check for
  /// nullptr.  Note that reference and const_reference are the same in our
  /// case.
  ///
  /// @note This is UB if a T has not been constructed in the storage (because
  /// it'll deref nullptr.)
  /// @param storage A storage holding a valid T.
  /// @return A reference to the underlying object.
  [[nodiscard]] static reference get(storage_type& storage);

  /// Marshall an aligned storage as a const reference.
  ///
  /// In our case we just dereference the pointer.  This does not check for
  /// nullptr.  Note that reference and const_reference are the same in our
  /// case.
  ///
  /// @note This is UB if a T has not been constructed in the storage (because
  /// it'll deref nullptr.)
  /// @param storage A storage holding a valid T.
  /// @return A reference to the underlying object.
  [[nodiscard]] static const_reference get(const storage_type& storage);

  static void get(storage_type&& storage) = delete;

  static void get(const storage_type&& storage) = delete;
};
} // namespace detail

/// Dial structure providing an interface to a view of (views of) input
/// messages.
///
/// @note The "view of views" is the general case, where we are returning not a
/// message exactly but rather an interface to a message.  This will be the case
/// most of the time in Clockwork -- you get an abstracted interface to the
/// message rather than the message itself. That said, this will work with
/// whatever type you instantiate it with.
///
/// @note As with most views, this is non-owning.  Under the hood it holds a
/// span of `const MsgViewType*`, which must be owned by something else for as
/// long as this view exists.
///
/// @tparam MsgViewType The type of the message view.
template <class MsgViewType, size_t max_size>
class MessageInputDial
{
  using CircularBuffer =
    jewels::container::CircularBuffer<detail::MsgPolicy<MsgViewType>, std::span<const MsgViewType*, max_size>>;

public:
  using MsgType = MsgViewType;
  static constexpr auto max_msgs = max_size;

  /// ViewType is the range view exposed to users.  It's guaranteed to provide
  /// O(1) random access and a size() method.
  using ViewType = std::ranges::ref_view<CircularBuffer>;

  /// This is the iterator type used by ViewType.  They will be random access iterators.
  using IteratorType = std::ranges::iterator_t<CircularBuffer>;

  MessageInputDial() = delete;

  /// Construct from a view, cursor iterator, and first_new iterator.
  /// @param buffer_view A non-owning view onto the underlying container.
  /// @param cursor An iterator to the "cursor" element for this input.
  /// @param first_new An iterator to the first new/unseen message in the view.
  constexpr MessageInputDial(ViewType buffer_view, IteratorType cursor, IteratorType first_new) noexcept;

  /// Access the full view
  [[nodiscard]] constexpr const ViewType& get_view() const noexcept;

  /// Access the cursor view
  [[nodiscard]] constexpr auto get_cursor_view() const noexcept;

  /// Get a view of all new messages view
  [[nodiscard]] constexpr auto get_new_msgs_view() const noexcept;

  /// Access the cursor iterator.  Might be end().
  [[nodiscard]] constexpr IteratorType get_cursor() const noexcept;

  /// Access the iterator to the first new/unseen message in the view.  Might be end().
  [[nodiscard]] constexpr IteratorType get_first_new() const noexcept;

  /// End iterator of the view.
  [[nodiscard]] constexpr IteratorType end() const noexcept;

protected:
  /// Set the cursor iterator.
  /// @note This is exposed only by the MessageInputDialWithCursorControl class (below).
  constexpr void set_cursor(IteratorType cursor) noexcept;

private:
  ViewType buffer_view_;
  IteratorType cursor_;
  IteratorType first_new_;
};

/// Version of a message input dial that allows the cursor to be set.
///
/// @note This is used only when the Cog has explicitly declared that it needs
/// manual cursor control.  Otherwise cursor control is automated.
template <class MsgType, size_t max_size>
class MessageInputDialWithCursorControl : public MessageInputDial<MsgType, max_size>
{
public:
  using MessageInputDial<MsgType, max_size>::MessageInputDial;
  using MessageInputDial<MsgType, max_size>::set_cursor;
};

} // namespace clockwork

#include "clockwork/dial/msg_input.inl"
