// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/pinion/device_ptr.hh"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>
#include <type_traits>
#include <utility>
#include <variant>

namespace clockwork
{

/// Non-owning view over an array of const pointers that auto-dereferences on iteration.
/// Semantically equivalent to std::span<const T> but backed by pointer indirection,
/// avoiding copies while presenting value-like iteration (`const T&`).
template <class T, bool expose_seqno_v, bool use_device_ptr_v>
class MsgView
{
public:
  static constexpr auto expose_seqno = expose_seqno_v;
  static constexpr auto use_device_ptr = use_device_ptr_v;

  /// Storage for message data and metadata
  struct Item
  {
    const T* message;
    [[no_unique_address]] std::conditional_t<expose_seqno, uint64_t, std::monostate> seqno{};
    [[no_unique_address]]
    std::conditional_t<use_device_ptr, pinion::DevicePtr<const T>, std::monostate> device_ptr{};
  };

  using InnerSpan = std::span<Item>;

  /// Random-access iterator that auto-dereferences through the pointer layer.
  class Iterator
  {
  public:
    using iterator_category = std::random_access_iterator_tag;
    using value_type = const T;
    using difference_type = ptrdiff_t;
    using pointer = const T*;
    using reference = const T&;

    constexpr Iterator() = default;
    constexpr explicit Iterator(typename InnerSpan::iterator iter)
      : it_(iter)
    {
    }

    constexpr const T& operator*() const
    {
      return *it_->message;
    }
    constexpr const T* operator->() const
    {
      return it_->message;
    }
    [[nodiscard]] constexpr uint64_t seqno() const
      requires(expose_seqno)
    {
      return it_->seqno;
    }
    constexpr pinion::DevicePtr<const T> device_ptr() const
      requires(use_device_ptr)
    {
      return std::move(it_->device_ptr);
    }

    constexpr Iterator& operator++()
    {
      ++it_;
      return *this;
    }
    constexpr Iterator operator++(int)
    {
      auto tmp = *this;
      ++it_;
      return tmp;
    }
    constexpr Iterator& operator--()
    {
      --it_;
      return *this;
    }
    constexpr Iterator operator--(int)
    {
      auto tmp = *this;
      --it_;
      return tmp;
    }

    constexpr Iterator& operator+=(difference_type n)
    {
      it_ += n;
      return *this;
    }
    constexpr Iterator& operator-=(difference_type n)
    {
      it_ -= n;
      return *this;
    }
    constexpr Iterator operator+(difference_type n) const
    {
      return Iterator{it_ + n};
    }
    constexpr Iterator operator-(difference_type n) const
    {
      return Iterator{it_ - n};
    }
    constexpr difference_type operator-(const Iterator& other) const
    {
      return it_ - other.it_;
    }
    constexpr const T& operator[](difference_type n) const
    {
      return *it_[n].message;
    }

    constexpr auto operator<=>(const Iterator&) const = default;
    constexpr bool operator==(const Iterator&) const = default;

    friend constexpr Iterator operator+(difference_type n, const Iterator& rhs)
    {
      return Iterator{rhs.it_ + n};
    }

  private:
    typename InnerSpan::iterator it_{};
  };

  constexpr MsgView() = default;
  constexpr MsgView(Item* data, size_t count)
    : inner_(data, count)
  {
  }

  [[nodiscard]] constexpr Iterator begin() const
  {
    return Iterator{inner_.begin()};
  }
  [[nodiscard]] constexpr Iterator end() const
  {
    return Iterator{inner_.end()};
  }
  [[nodiscard]] constexpr size_t size() const
  {
    return inner_.size();
  }
  [[nodiscard]] constexpr bool empty() const
  {
    return inner_.empty();
  }
  [[nodiscard]] constexpr const T& back() const
  {
    return *inner_.back().message;
  }
  [[nodiscard]] constexpr const T& front() const
  {
    return *inner_.front().message;
  }
  [[nodiscard]] constexpr const T& operator[](size_t idx) const
  {
    return *inner_[idx].message;
  }

private:
  InnerSpan inner_{};
};

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
/// @tparam max_size Maximum number of messages in the view.
/// @tparam min_messages Minimum number of messages guaranteed in the view (from execution conditions).
/// @tparam min_new_messages Minimum number of new messages guaranteed in the view.
/// @tparam manual_cursor_v When true, exposes set_cursor() for manual cursor control.
/// @tparam expose_seqno_v When true, exposes get_sequence_number() for reading message sequence numbers.
template <
  class MsgViewType,
  size_t max_size,
  size_t min_messages,
  size_t min_new_messages,
  bool manual_cursor_v = false,
  bool expose_seqno_v = false,
  bool use_device_ptr_v = false>
class MessageInputDial
{
public:
  using MsgType = MsgViewType;
  using MsgDevicePtr = pinion::DevicePtr<const MsgType>;
  static constexpr auto max_msgs = max_size;
  static constexpr auto min_msgs = min_messages;
  static constexpr auto min_new_msgs = min_new_messages;
  static constexpr auto manual_cursor = manual_cursor_v;
  static constexpr auto expose_seqno = expose_seqno_v;
  static constexpr auto use_device_ptr = use_device_ptr_v;

  /// ViewType is an auto-dereferencing view over externally-owned message pointers.
  /// Iteration yields `const MsgViewType&`, not pointers.
  using ViewType = MsgView<MsgViewType, expose_seqno, use_device_ptr>;
  using ViewItem = MsgView<MsgViewType, expose_seqno, use_device_ptr>::Item;

  /// Random-access iterator that auto-dereferences through the pointer layer.
  using IteratorType = typename ViewType::Iterator;

  MessageInputDial() = delete;

  /// Construct from a view, cursor iterator, and first_new iterator.
  /// @param buffer_view A non-owning view onto the underlying container.
  /// @param cursor An iterator to the "cursor" element for this input.
  /// @param first_new An iterator to the first new/unseen message in the view.
  /// @param connected Indicates if this input is connected to a channel.
  constexpr MessageInputDial(
    ViewType buffer_view, IteratorType cursor, IteratorType first_new, bool connected = true) noexcept;

  /// Construct from a view, cursor iterator, first_new iterator, and skip count.
  /// @param buffer_view A non-owning view onto the underlying container.
  /// @param cursor An iterator to the "cursor" element for this input.
  /// @param first_new An iterator to the first new/unseen message in the view.
  /// @param parameters Additional construction parameters
  constexpr MessageInputDial(
    ViewType buffer_view,
    IteratorType cursor,
    IteratorType first_new,
    size_t skip_count,
    bool connected = true) noexcept;

  /// Access the full view
  [[nodiscard]] constexpr const ViewType& get_view() const noexcept;

  /// Access the cursor view
  [[nodiscard]] constexpr auto get_cursor_view() const noexcept;

  /// Get a view of all new messages view
  [[nodiscard]] constexpr auto get_new_msgs_view() const noexcept;

  /// Get the latest message in the view, if it is guaranteed to exist.
  [[nodiscard]] constexpr auto& get_latest_msg() const noexcept
    requires(min_messages > 0);

  /// Get the latest new message in the view, if it is guaranteed to exist.
  [[nodiscard]] constexpr auto& get_latest_new_msg() const noexcept
    requires(min_new_messages > 0);

  /// Get a message view that is guaranteed to have at least one element because of an any_message or new_message
  /// execution condition.
  [[nodiscard]] constexpr auto get_nonempty_view() const noexcept
    requires(min_messages > 0);

  /// Get a view of new messages that is guaranteed to have at least one element because of an any_message execution
  /// condition.
  [[nodiscard]] constexpr auto get_nonempty_new_msgs_view() const noexcept
    requires(min_new_messages > 0);

  /// Access the cursor iterator.  Might be end().
  [[nodiscard]] constexpr IteratorType get_cursor() const noexcept;

  /// Access the iterator to the first new/unseen message in the view.  Might be end().
  [[nodiscard]] constexpr IteratorType get_first_new() const noexcept;

  /// End iterator of the view.
  [[nodiscard]] constexpr IteratorType end() const noexcept;

  /// @return If the input was configured with preemptive skipping and was fast-forwarded on this cycle, the number of
  /// messages that were skipped. Otherwise zero.
  [[nodiscard]] constexpr size_t num_messages_skipped() const noexcept;

  /// @return True if this input is connected to a channel.
  [[nodiscard]] constexpr bool connected() const noexcept;

  /// Set the cursor iterator.
  /// @note Only available when manual cursor control is enabled.
  constexpr void set_cursor(IteratorType cursor) noexcept
    requires(manual_cursor);

  /// Get the sequence number of the message at the given iterator position.
  /// @note Only available when sequence number exposure is enabled.
  /// @pre iter must be a valid dereferenceable iterator into get_view() (not end()).
  /// @param iter An iterator into the message view.
  /// @return The sequence number of the message at the given position.
  [[nodiscard]] constexpr uint64_t get_sequence_number(IteratorType iter) const noexcept
    requires(expose_seqno);

  [[nodiscard]] MsgDevicePtr device_ptr(IteratorType iter) const noexcept
    requires(use_device_ptr);

private:
  ViewType buffer_view_;
  IteratorType cursor_;
  IteratorType first_new_;
  size_t skip_count_{0};
  bool connected_{true};
};

} // namespace clockwork

#include "clockwork/dial/msg_input.inl"
