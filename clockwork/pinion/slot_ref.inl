// IWYU pragma: private, include "clockwork/pinion/slot_ref.hh"
#pragma once

#include "clockwork/pinion/slot_ref.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/buffer_index.hh" // IWYU pragma: keep
#include "clockwork/pinion/slot.hh"
#include "jewels/math/power_of_two.hh" // IWYU pragma: keep
#include "jewels/memory/pointers.hh"
#include "jewels/meta/concepts.hh"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>
#include <variant>

namespace clockwork::pinion
{
namespace detail
{
[[nodiscard]] bool MonostateSlotRefReader::is_sentinel() noexcept
{
  return true;
}

[[nodiscard]] bool MonostateSlotRefReader::is_valid() noexcept
{
  return false;
}

[[nodiscard]] ConstSlot MonostateSlotRefReader::slot() noexcept
{
  return get_empty_slot();
}

[[nodiscard]] ConstSlot MonostateSlotRefReader::slot(std::ptrdiff_t /*offset*/) noexcept
{
  return get_empty_slot();
}

uint64_t MonostateSlotRefReader::index() noexcept
{
  return 0;
}

void MonostateSlotRefReader::increment() noexcept {}

void MonostateSlotRefReader::decrement() noexcept {}

void MonostateSlotRefReader::advance(std::ptrdiff_t /*offset*/) noexcept {}

std::ptrdiff_t MonostateSlotRefReader::distance(const MonostateSlotRefReader& /*other*/) noexcept
{
  return 0;
}

template <typename BufferType>
BufferSlotRef<BufferType>::BufferSlotRef(
  jewels::memory::ObjectPtr<BufferType> buffer_ptr, const BufferIterator& buffer_iterator)
  : buffer_ptr_(buffer_ptr), buffer_iterator_(buffer_iterator)
{
}

template <typename BufferType>
[[nodiscard]] bool BufferSlotRef<BufferType>::is_sentinel() const noexcept
{
  return is_sentinel_iterator(buffer_iterator_);
}

template <typename BufferType>
[[nodiscard]] bool BufferSlotRef<BufferType>::is_valid() const noexcept
{
  return buffer_ptr_->still_available(buffer_iterator_);
}

template <typename BufferType>
[[nodiscard]] BufferSlotRef<BufferType>::SlotT BufferSlotRef<BufferType>::slot() const noexcept
{
  return buffer_iterator_.dereference();
}

template <typename BufferType>
[[nodiscard]] BufferSlotRef<BufferType>::SlotT BufferSlotRef<BufferType>::slot(std::ptrdiff_t offset) const noexcept
{
  auto iter = buffer_iterator_;
  iter.advance(offset);
  return iter.dereference();
}

template <typename BufferType>
inline uint64_t BufferSlotRef<BufferType>::index() const noexcept
{
  return buffer_iterator_.index();
}

template <typename BufferType>
void BufferSlotRef<BufferType>::increment() noexcept
{
  ++buffer_iterator_;
}

template <typename BufferType>
void BufferSlotRef<BufferType>::decrement() noexcept
{
  buffer_iterator_--;
}

template <typename BufferType>
void BufferSlotRef<BufferType>::advance(std::ptrdiff_t offset) noexcept
{
  buffer_iterator_.advance(offset);
}

template <typename BufferType>
[[nodiscard]] std::ptrdiff_t BufferSlotRef<BufferType>::distance(const BufferSlotRef& other) const noexcept
{
  return buffer_iterator_.distance_to(other.buffer_iterator_);
}

template <typename This, typename SlotT, typename... Impls>
inline SlotRefBase<This, SlotT, Impls...>::Pointer::Pointer(SlotT slot)
  : slot_(slot)
{
}

template <typename This, typename SlotT, typename... Impls>
inline const SlotT* SlotRefBase<This, SlotT, Impls...>::Pointer::operator->()
{
  return std::addressof(slot_);
}

template <typename This, typename SlotT, typename... Impls>
template <typename Impl>
inline SlotRefBase<This, SlotT, Impls...>::SlotRefBase(Impl&& impl)
  requires(jewels::meta::DecaysTo<Impl, Impls> || ...)
  : impl_(std::forward<Impl>(impl))
{
}

template <typename This, typename SlotT, typename... Impls>
[[nodiscard]] bool SlotRefBase<This, SlotT, Impls...>::is_sentinel() const noexcept
{
  return std::visit([](const auto& iter) { return iter.is_sentinel(); }, impl_);
}

template <typename This, typename SlotT, typename... Impls>
[[nodiscard]] bool SlotRefBase<This, SlotT, Impls...>::is_valid() const noexcept
{
  return std::visit([](const auto& iter) { return iter.is_valid(); }, impl_);
}

template <typename This, typename SlotT, typename... Impls>
[[nodiscard]] SlotT SlotRefBase<This, SlotT, Impls...>::slot() const noexcept
{
  return std::visit([](const auto& iter) { return iter.slot(); }, impl_);
}

template <typename This, typename SlotT, typename... Impls>
SlotT SlotRefBase<This, SlotT, Impls...>::operator*() const noexcept
{
  return slot();
}

template <typename This, typename SlotT, typename... Impls>
SlotRefBase<This, SlotT, Impls...>::Pointer SlotRefBase<This, SlotT, Impls...>::operator->() const noexcept
{
  return Pointer(std::visit([](const auto& iter) { return iter.slot(); }, impl_));
}

template <typename This, typename SlotT, typename... Impls>
This& SlotRefBase<This, SlotT, Impls...>::operator++() noexcept
{
  std::visit([](auto& iter) { return iter.increment(); }, impl_);
  return *static_cast<This*>(this);
}

template <typename This, typename SlotT, typename... Impls>
This SlotRefBase<This, SlotT, Impls...>::operator++(int)
{
  This tmp{*static_cast<This*>(this)};
  ++*this;
  return tmp;
}

template <typename This, typename SlotT, typename... Impls>
This& SlotRefBase<This, SlotT, Impls...>::operator--() noexcept
{
  std::visit([](auto& iter) { return iter.decrement(); }, impl_);
  return *static_cast<This*>(this);
}

template <typename This, typename SlotT, typename... Impls>
This SlotRefBase<This, SlotT, Impls...>::operator--(int)
{
  This tmp{*static_cast<This*>(this)};
  --*this;
  return tmp;
}

template <typename This, typename SlotT, typename... Impls>
inline This& SlotRefBase<This, SlotT, Impls...>::operator+=(std::ptrdiff_t offset) noexcept
{
  std::visit([offset](auto& iter) { return iter.advance(offset); }, impl_);
  return *static_cast<This*>(this);
}

template <typename This, typename SlotT, typename... Impls>
inline This& SlotRefBase<This, SlotT, Impls...>::operator-=(std::ptrdiff_t offset) noexcept
{
  return (*this) += (-offset);
}

template <typename This, typename SlotT, typename... Impls>
inline This SlotRefBase<This, SlotT, Impls...>::operator+(std::ptrdiff_t offset) const noexcept
{
  This tmp{*static_cast<const This*>(this)};
  std::visit([offset](auto& iter) { return iter.advance(offset); }, tmp.impl_);
  return tmp;
}

template <typename This, typename SlotT, typename... Impls>
inline This SlotRefBase<This, SlotT, Impls...>::operator-(std::ptrdiff_t offset) const noexcept
{
  return (*this) + (-offset);
}

template <typename This, typename SlotT, typename... Impls>
inline std::ptrdiff_t SlotRefBase<This, SlotT, Impls...>::operator-(const This& other) const noexcept
{
  return other.distance(*static_cast<const This*>(this));
}

template <typename This, typename SlotT, typename... Impls>
inline SlotT SlotRefBase<This, SlotT, Impls...>::operator[](std::ptrdiff_t offset) const noexcept
{
  return std::visit([offset](const auto& iter) { return iter.slot(offset); }, impl_);
}

template <typename This, typename SlotT, typename... Impls>
[[nodiscard]] std::ptrdiff_t SlotRefBase<This, SlotT, Impls...>::distance(const This& other) const noexcept
{
  bool sentinel_a = is_sentinel();
  bool sentinel_b = other.is_sentinel();
  if (sentinel_a != sentinel_b)
  {
    return std::numeric_limits<std::ptrdiff_t>::lowest();
  }
  if (sentinel_a)
  {
    return 0;
  }
  if (impl_.index() != other.impl_.index())
  {
    return std::numeric_limits<std::ptrdiff_t>::lowest();
  }
  return std::visit(
    [&other](const auto& iter) { return iter.distance(std::get<std::decay_t<decltype(iter)>>(other.impl_)); }, impl_);
}

template <typename This, typename SlotT, typename... Impls>
template <typename F>
inline auto SlotRefBase<This, SlotT, Impls...>::apply(F&& func) const noexcept
{
  return std::visit([func = std::forward<F>(func)](const auto& iter) { return func(iter); }, impl_);
}

template <typename This, typename SlotT, typename... Impls>
template <typename F>
inline auto SlotRefBase<This, SlotT, Impls...>::apply(F&& func) noexcept
{
  return std::visit([func = std::forward<F>(func)](auto& iter) { return func(iter); }, impl_);
}

template <typename This, typename SlotT, typename... Impls>
inline bool SlotRefBase<This, SlotT, Impls...>::operator==(const This& other) const noexcept
{
  return (distance(other) == 0);
}

template <typename This, typename SlotT, typename... Impls>
inline bool SlotRefBase<This, SlotT, Impls...>::operator!=(const This& other) const noexcept
{
  return (distance(other) != 0);
}

template <typename This, typename SlotT, typename... Impls>
inline bool SlotRefBase<This, SlotT, Impls...>::operator<(const This& other) const noexcept
{
  return (distance(other) > 0);
}

template <typename This, typename SlotT, typename... Impls>
inline bool SlotRefBase<This, SlotT, Impls...>::operator<=(const This& other) const noexcept
{
  return (distance(other) >= 0);
}

template <typename This, typename SlotT, typename... Impls>
inline bool SlotRefBase<This, SlotT, Impls...>::operator>(const This& other) const noexcept
{
  return (distance(other) < 0);
}

template <typename This, typename SlotT, typename... Impls>
inline bool SlotRefBase<This, SlotT, Impls...>::operator>=(const This& other) const noexcept
{
  return (distance(other) <= 0);
}

template <typename This, typename SlotT, typename... Impls>
inline This operator+(std::ptrdiff_t offset, const SlotRefBase<This, SlotT, Impls...>& iter) noexcept
{
  return iter + offset;
}

} // namespace detail

inline SlotRef::SlotRef()
  : SlotRefBase(detail::MonostateSlotRefReader())
{
}

inline SlotRef::SlotRef(::jewels::memory::ObjectPtr<const Buffer> buffer_ptr, const BufferIterator& buffer_iterator)
  : SlotRefBase(detail::BufferSlotRefReader(buffer_ptr, buffer_iterator))
{
}

[[nodiscard]] uint64_t SlotRef::index() const noexcept
{
  return apply([](const auto& iter) { return iter.index(); });
}

} // namespace clockwork::pinion
