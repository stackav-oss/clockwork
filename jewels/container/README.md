# Container library

## Circular buffer

The circular buffer is a fixed capacity sequence of elements behaving as if it were connected at both ends in a ring.

When adding / removing elements, the class needs to know how to handle the element storage.
For example, when removing an element it could call the destructor, set it to a sentinel value, or something else.
The circular buffer is designed with a customization point for modifying these behaviors.

The storage type also does not need to be the same as the value type.
For example, a circular buffer may have a value type of `T` but the backend storage type is a byte array of size `sizeof(T)`.
On access, the policy would handle conversion from the bytes to a `T` (e.g., by `reinterpret_cast`) and it would govern object lifetime when adding / removing elements.

In the `jewels/memory` library, we provide an `ObjectPolicy` class that is backed by an `AlignedStorage` type.
When adding an element, the policy calls placement-new and when removing an element the policy calls the object's destructor.
Unless there is a unique use case, the `ObjectPolicy` is a good policy to start with as it provides familiar semantics to how objects behave when adding / removing with other containers.

Currently, the backing storage must either be a `std::vector` or a `std::span`.

### Basic setup

Below is an example of creating a circular buffer of `int`s where the circular buffer operates on a `std::span` and the underlying storage is a `std::pmr::vector`.

```cpp
using Policy = jewels::memory::ObjectPolicy<int>;
std::pmr::vector<typename Policy::storage_type> storage{};
storage.resize(storage_size);
using CircBuffView = jewels::container::CircularBuffer<Policy, std::span<typename Policy::storage_type>>;
// This returns an expected type.  See class documentation on preconditions.
auto circ_buff = CircBuffView::try_make(std::span(storage));
```

This same example could apply to using a `std::array<typename Policy::storage_type, N>` (or any other contiguous container) as the `CircularBuffer` type only is aware of the `std::span`.

### Basic usage

Below are some examples of adding elements, removing elements, accessing elements, and checking the size of the container.

```cpp
// Add an element at the back.
circ_buff->emplace_back(1);
// Add an element at the front.
circ_buff->emplace_front(2);
// Access front element
std::cout << *std::begin(*circ_buff) << "\n"; // Prints 2
// Access back element
std::cout << *std::prev(std::end(*circ_buff)) << "\n"; // Prints 1
// Check the size
std::cout << circ_buff->size() << "\n"; // Prints 2
// Remove from back
circ_buff->pop_back();
// Remove from front
circ_buff->pop_front();
// Check if empty
std::cout << circ_buff->empty() << "\n"; // Prints true
```

### Custom policies

If you need a custom object policy, follow the API defined by `ObjectPolicy`.
In general, a policy must provide two sets of functions.
The first set is `construct` and `destruct` that govern the behavior of adding / removing elements.
The second set is accessors that provide methods to convert the underlying storage type to expected element type.

An example of a custom policy may be clearing a vector.
If you have a circular buffer where each element is a `std::vector<int>` then using the `ObjectPolicy` will result in a deallocation of memory when removing an element.
Users may want to reserve the memory for each element ahead of time and preserve that allocated memory.
To do so, create a custom policy that does nothing on `construct` and on `destruct` simply call `.clear()`.
Then any memory reserved ahead of time will be reused.

Here is an example of defining such a policy.

```cpp
template <class Container>
struct ClearPolicy
{
  using storage_type = Container;
  using value_type = storage_type;
  using reference = value_type&;
  using const_reference = const value_type&;

  static void construct(reference /*storage*/, const_reference /*value*/) {}

  static void destruct(reference storage)
  {
    storage.clear();
  }

  static reference get(reference storage)
  {
    return storage;
  }

  static const_reference get(const_reference storage)
  {
    return storage;
  }
};
```

Below is an example of using the custom policy.

```cpp
using Policy = ClearPolicy<std::pmr::vector<int>>;
// Notice now the storage element type is a vector instead of an aligned storage.
std::pmr::vector<std::pmr::vector<int>> storage{};
storage.resize(storage_size);
constexpr size_t reserve_size{8U};
for (auto& elem : storage)
{
  // Reserve memory ahead of time for each element.
  // This will be retained as `ClearPolicy` only clears the container when the element is removed.
  elem.reserve(reserve_size);
}
using CircBuffView = jewels::container::CircularBuffer<Policy, std::span<typename Policy::storage_type>>;
auto circ_buff = CircBuffView::try_make(std::span(storage));
```

### Saving and resuming from state

The circular buffer class provides methods for extracting the position state.
This is useful when wanting to pass the circular buffer in a Clockwork message.

```cpp
using Policy = jewels::memory::ObjectPolicy<int>;
std::pmr::vector<typename Policy::storage_type> storage{};
storage.resize(storage_size);
using CircBuffView = jewels::container::CircularBuffer<Policy, std::span<typename Policy::storage_type>>;
// This returns an expected type.  See class documentation on preconditions.
auto circ_buff = CircBuffView::try_make(std::span(storage));

// add / remove some elements
circ_buff->emplace_back();
circ_buff->emplace_back();
circ_buff->pop_front();

// Creates a new circular buffer pointing to identical storage with the same position state.
// These two containers would compare equal.
auto storage_copy = storage;
auto resumed_circ_buff = CircBuffView::try_make(std::span(storage_copy), circ_buff->state());
```

Notice in this example, the storage is copied first before resuming from the original state.
If instead we used a span over the original storage then both circular buffers would be mutating the same underlying data.
