// IWYU pragma: private, include "jewels/container/circular_buffer.hh"
#pragma once

#include "jewels/container/circular_buffer.hh"

#include "clockwork/repr_iface.hh"
#include "jewels/container/circular_buffer_state_clk_cc.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace jewels::container
{

inline CircularPosition::CircularPosition(std::ptrdiff_t position, uint8_t wrap)
  : position_{position}, wrap_{wrap}
{
}

inline std::ptrdiff_t CircularPosition::distance_to(const CircularPosition& other, std::ptrdiff_t size) const
{
  if (wrap_ == other.wrap_)
  {
    return other.position_ - position_;
  }
  auto diff = size - std::abs(position_ - other.position_);
  // Can't simply do `other.wrap_ > wrap_` because of overflow.
  auto wrap_copy = wrap_;
  if (other.wrap_ == ++wrap_copy)
  {
    return diff;
  }
  return -diff;
}

inline void CircularPosition::increment(std::ptrdiff_t size)
{
  ++position_;
  if (position_ == size)
  {
    ++wrap_;
    position_ = 0;
  }
}

inline void CircularPosition::decrement(std::ptrdiff_t size)
{
  if (position_ == 0)
  {
    --wrap_;
    position_ = size;
  }
  --position_;
}

inline void CircularPosition::increment(std::ptrdiff_t n, std::ptrdiff_t size)
{
  position_ += n;
  if (position_ >= size)
  {
    ++wrap_;
    position_ -= size;
  }
}

inline void CircularPosition::decrement(std::ptrdiff_t n, std::ptrdiff_t size)
{
  position_ -= n;
  if (position_ < 0)
  {
    --wrap_;
    position_ += size;
  }
}

inline std::ptrdiff_t CircularPosition::position() const
{
  return position_;
}

inline bool CircularPosition::at_max_delta_from(const CircularPosition& other) const
{
  // If the wraps are different but the positions are the same, then
  // this effectively means the container is full.  Or that these
  // positions are at their maximum delta.
  return wrap_ != other.wrap_ && position_ == other.position_;
}

template <class Policy, class Reference>
CircularIterator<Policy, Reference>::CircularIterator(std::span<SpanValueType> span, CircularPosition position)
  : span_{span}, position_{position}
{
}

template <class Policy, class Reference>
Reference CircularIterator<Policy, Reference>::dereference() const
{
  return Policy::get(span_[static_cast<size_t>(position_.position())]);
}

template <class Policy, class Reference>
bool CircularIterator<Policy, Reference>::equal(const CircularIterator& other) const
{
  return position_ == other.position_;
}

template <class Policy, class Reference>
CircularIterator<Policy, Reference>& CircularIterator<Policy, Reference>::increment()
{
  position_.increment(static_cast<std::ptrdiff_t>(span_.size()));
  return *this;
}

template <class Policy, class Reference>
CircularIterator<Policy, Reference>& CircularIterator<Policy, Reference>::decrement()
{
  position_.decrement(static_cast<std::ptrdiff_t>(span_.size()));
  return *this;
}

template <class Policy, class Reference>
CircularIterator<Policy, Reference>& CircularIterator<Policy, Reference>::advance(std::ptrdiff_t n)
{
  if (n > 0)
  {
    position_.increment(n, static_cast<std::ptrdiff_t>(span_.size()));
  }
  else
  {
    position_.decrement(-n, static_cast<std::ptrdiff_t>(span_.size()));
  }
  return *this;
}

template <class Policy, class Reference>
std::ptrdiff_t CircularIterator<Policy, Reference>::distance_to(const CircularIterator& other) const
{
  return position_.distance_to(other.position_, static_cast<std::ptrdiff_t>(span_.size()));
}

template <class Policy, class Reference>
Reference CircularIterator<Policy, Reference>::operator[](std::ptrdiff_t n) const
{
  // The boost iterator facade assumes the reference type is a proxy reference
  // and returns a type that can be casted to `Reference`.  In order to return
  // `Reference` directly instead, we need to make sure `Reference` is not a
  // proxy reference.  It's sufficient to test that `Reference` is actually a
  // reference type.
  static_assert(std::is_reference_v<Reference>, "Reference must not be a proxy reference.");
  auto copy = *this;
  copy.advance(n);
  return copy.dereference();
}

template <class Policy, class Container>
CircularBuffer<Policy, Container>::CircularBuffer(CircularBuffer&& other) noexcept
  : storage_(std::move(other.storage_)), begin_(other.begin_), end_(other.end_)
{
  // Set positions equal so the container operates as empty.
  other.end_ = other.begin_;
}

template <class Policy, class Container>
jewels::expected<CircularBuffer<Policy, Container>, CircularBufferConstructError>
CircularBuffer<Policy, Container>::try_make(size_t n, jewels::memory::MemoryResource resource)
{
  return try_make(std::pmr::vector<typename Policy::storage_type>(n, resource));
}

template <class Policy, class Container>
jewels::expected<CircularBuffer<Policy, Container>, CircularBufferConstructError>
CircularBuffer<Policy, Container>::try_make(Container&& storage)
{
  return try_make(
    std::move(storage),
    clockwork::Tappy<CircularBufferState>{
      clockwork::TapInit<clockwork::Tachyon<CircularBufferState>>{.offset = 0, .size = 0}});
}

template <class Policy, class Container>
jewels::expected<CircularBuffer<Policy, Container>, CircularBufferConstructError>
CircularBuffer<Policy, Container>::try_make(Container&& storage, clockwork::Tappy<CircularBufferState> state)
{
  if (storage.empty())
  {
    return jewels::unexpected{CircularBufferConstructError::empty_storage};
  }
  if (state.get_offset() >= storage.size())
  {
    return jewels::unexpected{CircularBufferConstructError::invalid_state_offset};
  }
  if (state.get_size() > storage.size())
  {
    return jewels::unexpected{CircularBufferConstructError::invalid_state_size};
  }
  return CircularBuffer{std::move(storage), state};
}

template <class Policy, class Container>
template <typename... Args>
CircularBuffer<Policy, Container>::CircularBuffer(std::in_place_t /*unused*/, Args&&... args)
  : storage_{std::forward<Args>(args)...}
{
  static_assert(constructor_safe_container_v<Container>);
}

template <class Policy, class Container>
template <class... Args>
auto CircularBuffer<Policy, Container>::emplace_back(Args&&... args)
  -> jewels::expected<iterator, CircularBufferEmplaceError>
{
  if (full())
  {
    return jewels::unexpected{CircularBufferEmplaceError::buffer_full};
  }
  return emplace_back_impl(std::forward<Args>(args)...);
}

template <class Policy, class Container>
template <class... Args>
auto CircularBuffer<Policy, Container>::force_emplace_back(Args&&... args) -> ForcedInsertion<iterator>
{
  const bool evicted{full()};
  if (evicted)
  {
    pop_front();
  }
  return {emplace_back_impl(std::forward<Args>(args)...), evicted};
}

template <class Policy, class Container>
template <class... Args>
auto CircularBuffer<Policy, Container>::emplace_back_impl(Args&&... args) -> iterator
{
  Policy::construct(storage_[static_cast<size_t>(end_.position())], std::forward<Args>(args)...);
  iterator iter{end()};
  end_.increment(static_cast<std::ptrdiff_t>(storage_.size()));
  return iter;
}

template <class Policy, class Container>
template <class... Args>
auto CircularBuffer<Policy, Container>::emplace_front(Args&&... args)
  -> jewels::expected<iterator, CircularBufferEmplaceError>
{
  if (full())
  {
    return jewels::unexpected{CircularBufferEmplaceError::buffer_full};
  }
  return emplace_front_impl(std::forward<Args>(args)...);
}

template <class Policy, class Container>
template <class... Args>
auto CircularBuffer<Policy, Container>::force_emplace_front(Args&&... args) -> ForcedInsertion<iterator>
{
  const bool evicted{full()};
  if (evicted)
  {
    pop_back();
  }
  return {emplace_front_impl(std::forward<Args>(args)...), evicted};
}

template <class Policy, class Container>
template <class... Args>
auto CircularBuffer<Policy, Container>::emplace_front_impl(Args&&... args) -> iterator
{
  begin_.decrement(static_cast<std::ptrdiff_t>(storage_.size()));
  auto& new_elem_storage = storage_[static_cast<size_t>(begin_.position())];
  Policy::construct(new_elem_storage, std::forward<Args>(args)...);
  return begin();
}

template <class Policy, class Container>
bool CircularBuffer<Policy, Container>::pop_front()
{
  if (begin_ == end_)
  {
    return false;
  }
  Policy::destruct(storage_[static_cast<size_t>(begin_.position())]);
  begin_.increment(static_cast<std::ptrdiff_t>(storage_.size()));
  return true;
}

template <class Policy, class Container>
bool CircularBuffer<Policy, Container>::pop_back()
{
  if (begin_ == end_)
  {
    return false;
  }
  end_.decrement(static_cast<std::ptrdiff_t>(storage_.size()));
  Policy::destruct(storage_[static_cast<size_t>(end_.position())]);
  return true;
}

template <class Policy, class Container>
auto CircularBuffer<Policy, Container>::begin() -> iterator
{
  return iterator{std::span{storage_}, begin_};
}

template <class Policy, class Container>
auto CircularBuffer<Policy, Container>::end() -> iterator
{
  return iterator{std::span{storage_}, end_};
}

template <class Policy, class Container>
auto CircularBuffer<Policy, Container>::begin() const -> const_iterator
{
  return cbegin();
}

template <class Policy, class Container>
auto CircularBuffer<Policy, Container>::end() const -> const_iterator
{
  return cend();
}

template <class Policy, class Container>
auto CircularBuffer<Policy, Container>::cbegin() const -> const_iterator
{
  return const_iterator{std::span{storage_}, begin_};
}

template <class Policy, class Container>
auto CircularBuffer<Policy, Container>::cend() const -> const_iterator
{
  return const_iterator{std::span{storage_}, end_};
}

template <class Policy, class Container>
size_t CircularBuffer<Policy, Container>::size() const
{
  return static_cast<size_t>(begin_.distance_to(end_, static_cast<std::ptrdiff_t>(storage_.size())));
}

template <class Policy, class Container>
bool CircularBuffer<Policy, Container>::empty() const
{
  return begin_ == end_;
}

template <class Policy, class Container>
void CircularBuffer<Policy, Container>::clear()
{
  while (pop_back())
  {
  }
}

template <class Policy, class Container>
bool CircularBuffer<Policy, Container>::full() const
{
  return begin_.at_max_delta_from(end_);
}

template <class Policy, class Container>
clockwork::Tappy<CircularBufferState> CircularBuffer<Policy, Container>::state() const
{
  return clockwork::Tappy<CircularBufferState>{clockwork::TapInit<clockwork::Tachyon<CircularBufferState>>{
    .offset = static_cast<size_t>(begin_.position()),
    .size = size(),
  }};
}

template <class Policy, class Container>
CircularBuffer<Policy, Container>::~CircularBuffer()
{
  clear();
}

template <class Policy, class Container>
CircularBuffer<Policy, Container>::CircularBuffer(Container&& storage, clockwork::Tappy<CircularBufferState> state)
  : storage_{std::move(storage)},
    begin_{static_cast<int64_t>(state.get_offset()), 0U},
    end_{static_cast<int64_t>(state.get_offset()), 0U}
{
  end_.increment(static_cast<std::ptrdiff_t>(state.get_size()), static_cast<std::ptrdiff_t>(storage_.size()));
}

} // namespace jewels::container
