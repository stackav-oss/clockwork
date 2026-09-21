// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/offboard/s3_read_streambuf.hh"
#include "clockwork/logging/onboard/tests/support/test_support.hh"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <istream>
#include <memory_resource>
#include <span>
#include <vector>

namespace clockwork_logging::offboard
{
namespace
{

TEST_CASE("S3ReadStreambuf")
{
  constexpr size_t buffer_size = 123U;

  std::array<char, buffer_size + 1U> wrbuf{};
  onboard::tests::fill_with_random_bytes(std::as_writable_bytes(std::span{wrbuf.data(), wrbuf.size()}));

  std::pmr::vector<std::byte> buffer;
  buffer.resize(buffer_size);

  S3ReadStreambuf sbuf{buffer};
  REQUIRE(sbuf.get_buffer_size() == buffer_size);

  SECTION("Write the entire buffer in one shot")
  {
    REQUIRE(
      sbuf.sputn(wrbuf.data(), static_cast<std::streamsize>(wrbuf.size())) ==
      static_cast<std::streamsize>(buffer_size));
    REQUIRE(std::memcmp(wrbuf.data(), buffer.data(), buffer_size) == 0);

    REQUIRE(sbuf.sputn(wrbuf.data(), static_cast<std::streamsize>(wrbuf.size())) == 0);
    REQUIRE(std::memcmp(wrbuf.data(), buffer.data(), buffer_size) == 0);

    std::memset(buffer.data(), 0, buffer_size);
    REQUIRE(sbuf.pubseekpos(0) == 0);

    REQUIRE(
      sbuf.sputn(wrbuf.data(), static_cast<std::streamsize>(wrbuf.size())) ==
      static_cast<std::streamsize>(buffer_size));
    REQUIRE(std::memcmp(wrbuf.data(), buffer.data(), buffer_size) == 0);
  }

  SECTION("Write the entire buffer one byte at a time")
  {
    for (size_t i = 0U; i < buffer_size; ++i)
    {
      REQUIRE(sbuf.sputn(&wrbuf.at(i), 1) == 1);
    }
    REQUIRE(sbuf.sputn(wrbuf.data(), 1) == 0);
    REQUIRE(std::memcmp(wrbuf.data(), buffer.data(), buffer_size) == 0);

    std::memset(buffer.data(), 0, buffer_size);
    REQUIRE(sbuf.pubseekpos(0) == 0);
    for (size_t i = 0U; i < buffer_size; ++i)
    {
      REQUIRE(sbuf.sputn(&wrbuf.at(i), 1) == 1);
    }
    REQUIRE(sbuf.sputn(wrbuf.data(), 1) == 0);
    REQUIRE(std::memcmp(wrbuf.data(), buffer.data(), buffer_size) == 0);
  }

  SECTION("Write the entire buffer in one shot through an iostream")
  {
    std::iostream ios{&sbuf};
    REQUIRE(ios.tellp() == 0);
    ios.write(wrbuf.data(), static_cast<std::streamsize>(buffer_size));
    REQUIRE(ios);
    REQUIRE(std::memcmp(wrbuf.data(), buffer.data(), buffer_size) == 0);

    ios.write(wrbuf.data(), static_cast<std::streamsize>(wrbuf.size()));
    REQUIRE_FALSE(ios);
    ios.clear();

    ios.seekp(0, std::ios_base::end);
    REQUIRE(ios.tellp() == static_cast<int64_t>(buffer_size));
    ios.seekp(0, std::ios_base::beg);
    REQUIRE(ios.tellp() == 0);

    std::memset(buffer.data(), 0, buffer_size);
    ios.write(wrbuf.data(), static_cast<std::streamsize>(buffer_size));
    REQUIRE(ios);
    REQUIRE(std::memcmp(wrbuf.data(), buffer.data(), buffer_size) == 0);
  }

  SECTION("Read the entire buffer one byte at a time through an iostream")
  {
    std::iostream ios{&sbuf};
    REQUIRE(ios.tellp() == 0);

    for (size_t i = 0U; i < buffer_size; ++i)
    {
      ios.put(static_cast<char>(wrbuf.at(i)));
      REQUIRE(ios);
      REQUIRE(ios.tellp() == static_cast<int64_t>(i + 1U));
    }
    REQUIRE(std::memcmp(wrbuf.data(), buffer.data(), buffer_size) == 0);

    ios.put('x');
    REQUIRE_FALSE(ios);

    ios.clear();
    ios.seekp(0);
    REQUIRE(ios.tellp() == 0);

    std::memset(buffer.data(), 0, buffer_size);
    for (size_t i = 0U; i < buffer_size; ++i)
    {
      ios.put(static_cast<char>(wrbuf.at(i)));
      REQUIRE(ios);
      REQUIRE(ios.tellp() == static_cast<int64_t>(i + 1U));
    }
    REQUIRE(std::memcmp(wrbuf.data(), buffer.data(), buffer_size) == 0);
  }
}

} // namespace
} // namespace clockwork_logging::offboard
