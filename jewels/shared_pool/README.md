# SharedObjectPool: Non-thread-safe pool of reference counted objects

The `SharedObjectPool` class implements a non-thread-safe pool of reference counted objects similar to `std::shared_ptr` except that all memory for the pool is allocated at initialization and the references are not thread safe.

## C++ example

```cpp
#include "jewels/shared_pool/shared_object_pool.hh"

// Get a shared pool of string objects
jewels::SharedObjectPool<std::pmr::string> shared_pool{memory_resource, 100};

// Get a shared string from the pool
auto get_result = shared_pool.make_shared_object("This is a test", memory_resource);
if (!get_result)
{
  // get_result.error is an error message
}
// Reference count on the string is 1
auto& string_ref = get_result.value();
auto another_string_ref = string_ref;
// Reference count on the string is 2
string_ref.release()
// Reference count is 1
another_string_ref.release();
// Reference count is zero, string is destroyed, and the object is returned to the pool
```
