// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/filesystem/path.hh"
#include "jewels/memory/default_memory_resource.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/testing/path_stringmakers.hh" // IWYU pragma: keep

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_tostring.hpp>

#include <string>

namespace Catch
{
TEST_CASE("Path Stringmakers")
{
  const auto memory_resource = jewels::memory::get_default_memory_resource();
  const jewels::filesystem::Path test_path{"/path/to/file", memory_resource};
  CHECK("/path/to/file" == StringMaker<jewels::filesystem::Path>::convert(test_path));
}
} // namespace Catch
