// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/repr_iface.hh"
#include "jewels/container/circular_buffer_state_clk_cc.hh"
#include "jewels/memory/aligned_storage.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/meta/call.hh"
#include "jewels/meta/type_traits.hh"
#include "jewels/std/expected.hh"

#include <boost/iterator/iterator_facade.hpp>
#include <wise_enum.h>

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace jewels::container
{

/// Error code when constructing a new circular buffer.
WISE_ENUM_CLASS((CircularBufferConstructError, uint8_t), empty_storage, invalid_state_offset, invalid_state_size);

/// Error code when emplacing an element.
WISE_ENUM_CLASS((CircularBufferEmplaceError, uint8_t), buffer_full);

/// A class used to represent the position of a circular buffer iterator.
/// @note This class does not hold the size of the container so it is
/// incomplete by itself.
class CircularPosition
{
public:
  /// Construct from components.
  /// @param position The position within the buffer.
  /// @param wrap The wrap count to differentiate between full and empty.
  inline CircularPosition(std::ptrdiff_t position, uint8_t wrap);

  /// Default constructor.
  CircularPosition() = default;

  /// Check distance between two positions.
  /// @param other The other position.
  /// @param size The size of the container.
  /// @return The number of times to increment (or decrement if negative) to get to other.
  [[nodiscard]] inline std::ptrdiff_t distance_to(const CircularPosition& other, std::ptrdiff_t size) const;

  /// Increment one position.
  /// @param size The size of the container.
  inline void increment(std::ptrdiff_t size);

  /// Decrement one position.
  /// @param size The size of the container.
  inline void decrement(std::ptrdiff_t size);

  /// Increment n positions.
  /// @pre n >= 0
  /// @pre n must not exceed the distance from this position to the end.
  /// @param n The number of positions to increment.
  /// @param size The size of the container.
  inline void increment(std::ptrdiff_t n, std::ptrdiff_t size);

  /// Decrement n positions.
  /// @pre n >= 0
  /// @pre n must not exceed the distance from begining to this position.
  /// @param n The number of positions to decrement.
  /// @param size The size of the container.
  inline void decrement(std::ptrdiff_t n, std::ptrdiff_t size);

  /// Get the current position.
  /// @return The position.
  [[nodiscard]] inline std::ptrdiff_t position() const;

  /// Check if the positions are at the maximum delta.  If so, this implies the container would be full.
  /// @param other The other position.
  /// @param True if at the maximum delta.
  [[nodiscard]] inline bool at_max_delta_from(const CircularPosition& other) const;

private:
  /// Check for equality of two positions.
  /// @param lhs One position.
  /// @param rhs The other position.
  /// @return True if the two positions are equal and false otherwise.
  [[nodiscard]] friend bool operator==(const CircularPosition& lhs, const CircularPosition& rhs)
  {
    return lhs.position_ == rhs.position_ && lhs.wrap_ == rhs.wrap_;
  }

  /// The current position.
  std::ptrdiff_t position_{};

  /// Current wrap count.  Two positions for the same container should
  /// have a maximum absolute difference in wrap counts of 1.
  uint8_t wrap_{0U};
};

/// An iterator class for a circular buffer.
template <class Policy, class Reference>
class CircularIterator : public boost::iterator_facade<
                           CircularIterator<Policy, Reference>,
                           typename Policy::value_type,
                           std::random_access_iterator_tag,
                           Reference>
{
  // Match constness of value type to the reference type.
  using SpanValueType = jewels::meta::Call<
    jewels::meta::ConditionalT<std::is_const_v<std::remove_reference_t<Reference>>>,
    const typename Policy::storage_type,
    typename Policy::storage_type>;

public:
  /// Construct an iterator from a span over storages and a starting position.
  /// @param span A span over storages.
  /// @param position The starting position of the iterator.
  CircularIterator(std::span<SpanValueType> span, CircularPosition position);

  /// Default construct a circular iterator.
  CircularIterator() = default;

  /// Deference the iterator.
  /// @param A reference to the underlying object.
  [[nodiscard]] Reference dereference() const;

  /// Check equality of two iterators.
  /// @param other The other iterator.
  /// @return True if equal and false otherwise.
  [[nodiscard]] bool equal(const CircularIterator& other) const;

  /// Increment one position.
  /// @return A reference to the current iterator after incrementing.
  CircularIterator& increment();

  /// Decrement one position.
  /// @return A reference to the current iterator after decrementing.
  CircularIterator& decrement();

  /// Advance the iterator n positions.
  /// @param n The number of positions to advance (can be positive or negative).
  /// @return A reference to the iterator after advancing.
  CircularIterator& advance(std::ptrdiff_t n);

  /// Distance to another iterator.
  /// @param other The other iterator.
  /// @return The number of times to increment (or decrement if negative) to get to other.
  [[nodiscard]] std::ptrdiff_t distance_to(const CircularIterator& other) const;

  /// Provide operator[] as the one provided by the iterator facade doesn't
  /// satisfy std::ranges::random_access_range.
  Reference operator[](std::ptrdiff_t n) const;

private:
  /// A span over the storages.
  std::span<SpanValueType> span_;
  /// Current position of the iterator.
  CircularPosition position_;
};

/// The return type of forced insertion.
template <class Iterator>
struct ForcedInsertion
{
  /// Iterator to newly inserted element.
  Iterator iter;
  /// Whether or not an element needed to be evicted before inserting the new one.
  bool evicted;
};

/// Supported containers
template <class Container>
inline constexpr bool supported_container_v{false};

template <class T, class Allocator>
inline constexpr bool supported_container_v<std::vector<T, Allocator>>{true};

template <class T, auto size>
inline constexpr bool supported_container_v<std::span<T, size>>{true};

/// Containers that are safe to make without error checking
template <class Container>
inline constexpr bool constructor_safe_container_v{false};

template <class T, auto size>
inline constexpr bool constructor_safe_container_v<std::span<T, size>>{size != 0U && size != std::dynamic_extent};

/// A circular buffer class.
/// @tparam T The value type of the container.
/// @tparam Policy A class with static members for construct and
/// destruct that govern construction and destruction of each object
/// in the buffer.  See jewels::memory::ObjectPolicy for an example.
/// @tparam Container The underlying container to hold the storage objects (can be also be a view).
template <class Policy, class Container = std::pmr::vector<typename Policy::storage_type>>
class CircularBuffer
{
  // Adding new containers might require updating the move / copy constructors and assignment operators.
  static_assert(supported_container_v<Container>, "Unsupported container.  Be mindful of adding new containers.");
  // With only vector and span supported, move construction should
  // always be noexcept.  Note that T may not be noexcept moveable,
  // but T never needs to get moved.  If the supported containers
  // change, such as including std::array, then this needs to change.
  static_assert(std::is_nothrow_move_constructible_v<Container>);

  static_assert(
    std::is_same_v<typename Policy::storage_type, typename Container::value_type>,
    "Mismatched container and policy types.");

public:
  using value_type = typename Policy::value_type;
  using reference = typename Policy::reference;
  using const_reference = typename Policy::const_reference;
  using iterator = CircularIterator<Policy, reference>;
  using const_iterator = CircularIterator<Policy, const_reference>;

  /// Public constructor for constructor safe storage types
  /// @tparam Args Constructor argument types
  /// @param[in] args Constructor arguments
  template <typename... Args>
  explicit CircularBuffer(std::in_place_t /*unused*/, Args&&... args);

  /// Move constructor.
  CircularBuffer(CircularBuffer&& /*other*/) noexcept;

  /// Delete all constructors and operator= that could result in a
  /// copy or need for moving individual elements.  The move
  /// assignment operator could result in a need to move individual
  /// elements if the allocators for a std::pmr container are not
  /// equal.  These could be enabled with some additional support.
  CircularBuffer(const CircularBuffer&) = delete;
  CircularBuffer& operator=(const CircularBuffer&) = delete;
  CircularBuffer& operator=(CircularBuffer&&) = delete;

  /// Create a circular buffer given a size and a memory resource.
  /// @pre n > 0
  /// @param n The number of objects to hold.
  /// @param resource The resource to allocate space from.
  /// @return A valid CircularBuffer if construction was successful.
  [[nodiscard]] static jewels::expected<CircularBuffer, CircularBufferConstructError>
  try_make(size_t n, jewels::memory::MemoryResource resource);

  /// Create a circular buffer given a preallocated storage.
  /// @pre storage is not empty.
  /// @param storage The existing storage.
  /// @return A valid CircularBuffer if construction was successful.
  [[nodiscard]] static jewels::expected<CircularBuffer, CircularBufferConstructError> try_make(Container&& storage);

  /// Create a circular buffer given a preallocated storage and a specified state to resume from.
  /// @pre storage is not empty and state is valid given the container size.
  /// @param storage The existing storage.
  /// @param state The state to resume from.
  /// @return A valid CircularBuffer if construction was successful.
  [[nodiscard]] static jewels::expected<CircularBuffer, CircularBufferConstructError>
  try_make(Container&& storage, clockwork::Tappy<CircularBufferState> state);

  /// Try to emplace an element at the back of the container.  Will fail if full.
  /// @param args A pack of args used to construct the new element.
  /// @return An iterator to the newly emplaced element if successful.
  template <class... Args>
  jewels::expected<iterator, CircularBufferEmplaceError> emplace_back(Args&&... args);

  /// If full, pop an element from the front and emplace an element at the back.
  /// @param args A pack of args used to construct the new element.
  /// @return An iterator to the new element and a flag if an element was evicted or not.
  template <class... Args>
  ForcedInsertion<iterator> force_emplace_back(Args&&... args);

  /// Try to emplace an element at the front  of the container.  Will fail if full.
  /// @param args A pack of args used to construct the new element.
  /// @return An iteratorto the newly emplaced element if successful.
  template <class... Args>
  jewels::expected<iterator, CircularBufferEmplaceError> emplace_front(Args&&... args);

  /// If full, pop an element from the back and emplace an element at the front.
  /// @param args A pack of args used to construct the new element.
  /// @return An iterator to the new element and a flag if an element was evicted or not.
  template <class... Args>
  ForcedInsertion<iterator> force_emplace_front(Args&&... args);

  /// Pop one element from the front.  If empty, nothing happens.
  /// @return True if an element was popped and false otherwise.
  bool pop_front();

  /// Pop one element from the back.  If empty, nothing happens.
  /// @return True if an element was popped and false otherwise.
  bool pop_back();

  /// Create an iterator to the first element in the container.
  /// @return The iterator.
  [[nodiscard]] iterator begin();

  /// Create an iterator to one past the last element in the container.
  /// @return The iterator.
  [[nodiscard]] iterator end();

  /// Create a const iterator to the first element in the container.
  /// @return The iterator.
  [[nodiscard]] const_iterator begin() const;

  /// Create a const iterator to one past the last element in the container.
  /// @return The iterator.
  [[nodiscard]] const_iterator end() const;

  /// Create a const iterator to the first element in the container.
  /// @return The iterator.
  [[nodiscard]] const_iterator cbegin() const;

  /// Create a const iterator to one past the last element in the container.
  /// @return The iterator.
  [[nodiscard]] const_iterator cend() const;

  /// Get the size of the container.
  /// @return The size.
  [[nodiscard]] size_t size() const;

  /// Check if the container is empty.
  /// @return True if empty and false otherwise.
  [[nodiscard]] bool empty() const;

  /// Clear the container.
  void clear();

  /// Check if the container is full.
  [[nodiscard]] bool full() const;

  /// Get the state of the circular buffer positions.
  /// This can be used to save and resume when using an external storage.
  [[nodiscard]] clockwork::Tappy<CircularBufferState> state() const;

  /// Destruct any elements in the container.
  ~CircularBuffer();

private:
  /// Construct a buffer. Meant to be called only by the static try_make functions.
  explicit CircularBuffer(Container&& storage, clockwork::Tappy<CircularBufferState> state);

  /// Helper function to emplace an element in the back of the container.
  /// @pre Assumes the container is not full.
  /// @param args A pack of args used to construct the new element.
  /// @return An iterator to the newly emplaced element.
  template <class... Args>
  iterator emplace_back_impl(Args&&... args);

  /// Helper function to emplace an element in the front of the container.
  /// @pre Assumes the container is not full.
  /// @param args A pack of args used to construct the new element.
  /// @return An iterator to the newly emplaced element.
  template <class... Args>
  iterator emplace_front_impl(Args&&... args);

  /// The storage for the objects.
  Container storage_;

  /// Current begin position.
  /// @note Storing a raw position instead of an iterator to save on object size.
  CircularPosition begin_;

  /// Current end position.
  /// @note Storing a raw position instead of an iterator to save on object size.
  CircularPosition end_;
};

} // namespace jewels::container

#include "jewels/container/circular_buffer.inl"
