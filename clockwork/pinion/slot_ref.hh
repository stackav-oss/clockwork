// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/slot.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/meta/concepts.hh"

#include <cstdint>
#include <iosfwd>
#include <iterator>
#include <type_traits>
#include <variant>

namespace clockwork::pinion
{
namespace detail
{
///
/// An implementation for default constructed SlotRef.  Always sentinel / invalid.
///
class MonostateSlotRefReader
{
public:
  MonostateSlotRefReader() = default;

  /// Checks if this is a null (default constructed, etc) iterator
  static inline bool is_sentinel() noexcept;

  /// Checks if the slot referenced by this iterator exists.
  static inline bool is_valid() noexcept;

  /// Gets the slot at the current iterator position
  static inline ConstSlot slot() noexcept;

  /// Gets a slot with an offset
  static inline ConstSlot slot(std::ptrdiff_t offset) noexcept;

  /// Shifts the iterator to the next position
  static inline void increment() noexcept;

  /// Shifts the iterator to the previous position
  static inline void decrement() noexcept;

  /// Moves the position by \p offset
  /// `advance(1)` should behave like `increment()`
  static inline void advance(std::ptrdiff_t offset) noexcept;

  /// Compute the number of positions between this and \p other
  /// This should operate such that `it_a.advance(it_a.distance(it_b)) == it_b`.
  static inline std::ptrdiff_t distance(const MonostateSlotRefReader& other) noexcept;

  /// Returns the underlying position of the iterator in the channel for debugging purposes.
  static inline uint64_t index() noexcept;

  /// Gets an empty slot that points to a static memory region that may be used to provide an empty return for sentinel
  /// iterators.
  static Slot get_empty_slot();
};

///
/// An implementation of SlotRefReader to use with Buffer backed channels
///
class BufferSlotRefReader
{
public:
  explicit inline BufferSlotRefReader(
    jewels::memory::ObjectPtr<const Buffer> buffer_ptr, const BufferIterator& buffer_iterator);
  [[nodiscard]] inline bool is_sentinel() const noexcept;
  [[nodiscard]] inline bool is_valid() const noexcept;
  [[nodiscard]] inline ConstSlot slot() const noexcept;
  [[nodiscard]] inline ConstSlot slot(std::ptrdiff_t offset) const noexcept;
  [[nodiscard]] inline uint64_t index() const noexcept;
  inline void increment() noexcept;
  inline void decrement() noexcept;
  inline void advance(std::ptrdiff_t offset) noexcept;
  [[nodiscard]] inline std::ptrdiff_t distance(const BufferSlotRefReader& other) const noexcept;

private:
  jewels::memory::ObjectPtr<const Buffer> buffer_ptr_;
  BufferIterator buffer_iterator_;
};

template <typename This, typename SlotT, typename... Impls>
class SlotRefBase
{
public:
  using difference_type = std::ptrdiff_t;
  using value_type = SlotT;
  using reference = SlotT;
  using iterator_category = std::random_access_iterator_tag;
  // operator-> is a problem because it needs to return an lvalue but there isn't a place to store the slot. This
  // intermediate type causes the compiler to put the rvalue on the stack and then call its operator-> which resolves
  // the problem.
  class Pointer
  {
  public:
    explicit inline Pointer(SlotT slot);
    inline const SlotT* operator->();

  private:
    SlotT slot_;
  };

  template <typename Impl>
  explicit inline SlotRefBase(Impl&& impl)
    requires(jewels::meta::DecaysTo<Impl, Impls> || ...);

  /// Test whether the reference is the sentinel, which means either that the internal implementation is nullptr, or
  /// the implementation is present and claims to be ther sentinel for its type.
  [[nodiscard]] inline bool is_sentinel() const noexcept;

  /// Test whether the message buffer exists and is valid
  /// @return True if the buffer slot for the message is still available
  [[nodiscard]] inline bool is_valid() const noexcept;

  /// Returns the Slot that this reference represents
  [[nodiscard]] inline SlotT slot() const noexcept;

  /// Iterator interface
  /// @{
  inline SlotT operator*() const noexcept;
  inline Pointer operator->() const noexcept;
  inline This& operator++() noexcept;
  inline This operator++(int);
  inline This& operator--() noexcept;
  inline This operator--(int);
  inline This& operator+=(std::ptrdiff_t offset) noexcept;
  inline This& operator-=(std::ptrdiff_t offset) noexcept;
  inline This operator+(std::ptrdiff_t offset) const noexcept;
  inline This operator-(std::ptrdiff_t offset) const noexcept;
  inline std::ptrdiff_t operator-(const This& other) const noexcept;
  inline SlotT operator[](std::ptrdiff_t offset) const noexcept;
  inline bool operator==(const This& other) const noexcept;
  inline bool operator!=(const This& other) const noexcept;
  inline bool operator<(const This& other) const noexcept;
  inline bool operator<=(const This& other) const noexcept;
  inline bool operator>(const This& other) const noexcept;
  inline bool operator>=(const This& other) const noexcept;
  /// @}

protected:
  [[nodiscard]] inline std::ptrdiff_t distance(const This& other) const noexcept;

  template <typename F>
  inline auto apply(F&& func) const noexcept;
  template <typename F>
  inline auto apply(F&& func) noexcept;

private:
  static_assert((std::is_nothrow_move_constructible_v<Impls> && ...));

  std::variant<Impls...> impl_;
};

template <typename This, typename SlotT, typename... Impls>
inline This operator+(std::ptrdiff_t offset, const SlotRefBase<This, SlotT, Impls...>& iter) noexcept;

} // namespace detail

///
/// An abstract reference to track a const message slot
///
class SlotRef
  : public detail::SlotRefBase<SlotRef, ConstSlot, detail::MonostateSlotRefReader, detail::BufferSlotRefReader>
{
public:
  inline SlotRef();
  inline SlotRef(::jewels::memory::ObjectPtr<const Buffer> buffer_ptr, const BufferIterator& buffer_iterator);

  /// Returns an index that can be used for debugging purposes.  The exact meaning is implementation defined, but
  /// it should explain the position of the message in the channel.  Note that this is distinct from the slot's
  /// sequence number if messages have been dropped.
  [[nodiscard]] inline uint64_t index() const noexcept;
};

} // namespace clockwork::pinion

#include "clockwork/pinion/slot_ref.inl"
