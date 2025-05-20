// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/utility/fix_clockwork_path.hh"

#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/filesystem.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"

#include <memory_resource>
#include <string>
#include <string_view>

namespace jewels
{

[[nodiscard]] std::string fix_clockwork_path(std::string_view file_path)
{
  filesystem::Filesystem filesys{memory::MemoryResource(std::pmr::get_default_resource())};
  if (const auto exists_result = filesys.exists(file_path); exists_result && exists_result.value())
  {
    return std::string(file_path);
  }
  auto fixed_path = std::string("../clockwork~/").append(file_path);
  if (const auto exists_result = filesys.exists(fixed_path); exists_result && exists_result.value())
  {
    return fixed_path;
  }
  return std::string(file_path);
}

} // namespace jewels
