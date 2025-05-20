# Filesystem-related libraries for onboard use

The classes in this directory implement a subset of std::filesystem suitable for onboard use.

## Filesystem

Filesystem implements wrappers around system calls to implement the filesystem interface.
Any memory allocations for operations like read_directory or read_symlink are done through a memory_resource passed to the constructor.
All errors are reported through a jewels::expected\<T, ErrorCode>.
All path parameters take std::string_view types.

There is an error injection wrapper class in jewels::testing::FilesystemWrapper that has the same interface as Filesystem but adds methods for injecting errors in future calls to the filesystem methods.

## Path

Path implements a subset of the features of std::filesystem::path, leaving out the windows compatibility features and iterators.
The path is stored in a std::pmr::string using a memory resource passed to the constructor.
Path borrows some features from std::string, including comparison operators with null terminated character arrays and implicit conversion to std::string_view.

## FileDescriptor

FileDescriptor is an RAII wrapper around an open file handle stored in an int32_t.

## C++ example

```cpp
#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/filesystem/filesystem.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/memory/memory_resource.hh"

jewels::memory::jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
filesystem kits_fs{memory_resource};

const auto file_path = filesystem::Path{"/example"} / "file.txt";

auto open_result = kits_fs.open(file_path, O_CREAT | O_EXCL | O_WRONLY);
if (!open_result)
{
  // open_result.error() returns a ErrorCode
}
auto& file_desc = *open_result;

if (const auto write_result = kits_fs.write(file_desc, gsl::as_bytes(buffer.data(), buffer.size())); !write_result)
{
  // handle error
}

// Calling close is recommended for writing in case of errors while writing from the buffer cache
if (!const auto close_result = file_desc.close(); !close_result)
{
  // alternatively file_desc closes when it goes out of scope
}
```
