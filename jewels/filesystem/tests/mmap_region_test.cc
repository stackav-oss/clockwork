// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/filesystem/mmap_region.hh"
#include "jewels/std/expected.hh"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstring>
#include <fcntl.h>
#include <span>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>

namespace jewels::filesystem
{

TEST_CASE("MMapRegion")
{
  constexpr off_t size = 8;

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) C / platform API required
  const FileDescriptor file{::open("/dev/shm", O_TMPFILE | O_RDWR | O_CLOEXEC)};
  REQUIRE(file);

  REQUIRE(::ftruncate(*file, size) != -1);

  constexpr std::array<char, size> testdata{{"abcdefg"}};

  // Use mmap to write file contents
  // Technically writes are only guaranteed to be visible on the file after munmap (or msync) so destruct the mapping
  // before checking
  {
    auto expected_region = MMapRegion::create(*file, size, PROT_WRITE, MAP_SHARED);
    REQUIRE(expected_region);
    auto region = std::move(*expected_region);
    REQUIRE(region);
    CHECK(!*expected_region);

    auto span = region.to_span();
    REQUIRE(span.size() == size);
    memcpy(span.data(), testdata.data(), size);
  }

  std::array<char, size> buffer{};
  REQUIRE(::read(*file, buffer.data(), size) == size);
  CHECK(buffer == testdata);
}

} // namespace jewels::filesystem
