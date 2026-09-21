// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/offboard/s3_write_streambuf.hh"
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

TEST_CASE("S3WriteStreambuf")
{
  constexpr size_t buffer1_size = 123U;
  constexpr size_t buffer2_size = 234U;
  constexpr size_t buffer3_size = 345U;
  constexpr auto total_size = buffer1_size + buffer2_size + buffer3_size;

  std::pmr::vector<std::byte> buffer1;
  buffer1.resize(buffer1_size);
  onboard::tests::fill_with_random_bytes(buffer1);
  std::pmr::vector<std::byte> buffer2;
  buffer2.resize(buffer2_size);
  onboard::tests::fill_with_random_bytes(buffer2);
  std::pmr::vector<std::byte> buffer3;
  buffer3.resize(buffer3_size);
  onboard::tests::fill_with_random_bytes(buffer3);

  S3WriteStreambuf sbuf{std::pmr::vector<std::pmr::vector<std::byte>>{buffer1, buffer2, buffer3}};
  REQUIRE(sbuf.total_size() == total_size);

  SECTION("Read the entire buffer in one shot")
  {
    std::array<char, total_size + 1U> rdbuf{};
    REQUIRE(
      sbuf.sgetn(rdbuf.data(), static_cast<std::streamsize>(rdbuf.size())) == static_cast<std::streamsize>(total_size));
    REQUIRE(std::memcmp(rdbuf.data(), buffer1.data(), buffer1_size) == 0);
    REQUIRE(std::memcmp(&rdbuf.at(buffer1_size), buffer2.data(), buffer2_size) == 0);

    REQUIRE(sbuf.sgetn(rdbuf.data(), static_cast<std::streamsize>(rdbuf.size())) == 0);

    rdbuf = {};
    REQUIRE(sbuf.pubseekpos(0) == 0);
    REQUIRE(
      sbuf.sgetn(rdbuf.data(), static_cast<std::streamsize>(rdbuf.size())) == static_cast<std::streamsize>(total_size));
    REQUIRE(std::memcmp(rdbuf.data(), buffer1.data(), buffer1_size) == 0);
    REQUIRE(std::memcmp(&rdbuf.at(buffer1_size), buffer2.data(), buffer2_size) == 0);
    REQUIRE(std::memcmp(&rdbuf.at(buffer1_size + buffer2_size), buffer3.data(), buffer3_size) == 0);
  }

  SECTION("Read the entire buffer one byte at a time")
  {
    char buf{};

    for (size_t i = 0U; i < buffer1_size; ++i)
    {
      REQUIRE(sbuf.sgetn(&buf, 1) == 1);
      REQUIRE(static_cast<std::byte>(buf) == buffer1.at(i));
    }
    for (size_t i = 0U; i < buffer2_size; ++i)
    {
      REQUIRE(sbuf.sgetn(&buf, 1) == 1);
      REQUIRE(static_cast<std::byte>(buf) == buffer2.at(i));
    }
    for (size_t i = 0U; i < buffer3_size; ++i)
    {
      REQUIRE(sbuf.sgetn(&buf, 1) == 1);
      REQUIRE(static_cast<std::byte>(buf) == buffer3.at(i));
    }

    REQUIRE(sbuf.sgetn(&buf, 1) == 0);

    REQUIRE(sbuf.pubseekpos(0) == 0);
    for (size_t i = 0U; i < buffer1_size; ++i)
    {
      REQUIRE(sbuf.sgetn(&buf, 1) == 1);
      REQUIRE(static_cast<std::byte>(buf) == buffer1.at(i));
    }
    for (size_t i = 0U; i < buffer2_size; ++i)
    {
      REQUIRE(sbuf.sgetn(&buf, 1) == 1);
      REQUIRE(static_cast<std::byte>(buf) == buffer2.at(i));
    }
    for (size_t i = 0U; i < buffer3_size; ++i)
    {
      REQUIRE(sbuf.sgetn(&buf, 1) == 1);
      REQUIRE(static_cast<std::byte>(buf) == buffer3.at(i));
    }
  }

  SECTION("Read the entire buffer in one shot through an iostream")
  {
    std::array<char, total_size + 1U> rdbuf{};
    std::iostream ios{&sbuf};
    REQUIRE(ios.tellg() == 0);
    ios.read(rdbuf.data(), static_cast<std::streamsize>(rdbuf.size()));
    REQUIRE(ios.gcount() == static_cast<std::streamsize>(total_size));
    REQUIRE(std::memcmp(rdbuf.data(), buffer1.data(), buffer1_size) == 0);
    REQUIRE(std::memcmp(&rdbuf.at(buffer1_size), buffer2.data(), buffer2_size) == 0);
    REQUIRE(std::memcmp(&rdbuf.at(buffer1_size + buffer2_size), buffer3.data(), buffer3_size) == 0);

    ios.read(rdbuf.data(), static_cast<std::streamsize>(rdbuf.size()));
    REQUIRE(ios.gcount() == 0);
    ios.clear();

    ios.seekg(0, std::ios_base::end);
    REQUIRE(ios.tellg() == static_cast<int64_t>(total_size));
    ios.seekg(0, std::ios_base::beg);
    REQUIRE(ios.tellg() == 0);

    rdbuf = {};
    ios.read(rdbuf.data(), static_cast<std::streamsize>(rdbuf.size()));
    REQUIRE(ios.gcount() == static_cast<std::streamsize>(total_size));
    REQUIRE(std::memcmp(rdbuf.data(), buffer1.data(), buffer1_size) == 0);
    REQUIRE(std::memcmp(&rdbuf.at(buffer1_size), buffer2.data(), buffer2_size) == 0);
    REQUIRE(std::memcmp(&rdbuf.at(buffer1_size + buffer2_size), buffer3.data(), buffer3_size) == 0);
  }

  SECTION("Read the entire buffer one byte at a time through an iostream")
  {
    char value{};
    std::iostream ios{&sbuf};
    REQUIRE(ios.tellg() == 0);

    for (size_t i = 0U; i < buffer1_size; ++i)
    {
      ios.get(value);
      REQUIRE(static_cast<std::byte>(value) == buffer1.at(i));
      REQUIRE(ios.gcount() == 1);
      REQUIRE(ios.tellg() == static_cast<int64_t>(i + 1U));
    }
    for (size_t i = 0U; i < buffer2_size; ++i)
    {
      ios.get(value);
      REQUIRE(static_cast<std::byte>(value) == buffer2.at(i));
      REQUIRE(ios.gcount() == 1);
      REQUIRE(ios.tellg() == static_cast<int64_t>(buffer1_size + i + 1U));
    }
    for (size_t i = 0U; i < buffer3_size; ++i)
    {
      ios.get(value);
      REQUIRE(static_cast<std::byte>(value) == buffer3.at(i));
      REQUIRE(ios.gcount() == 1);
      REQUIRE(ios.tellg() == static_cast<int64_t>(buffer1_size + buffer2_size + i + 1U));
    }

    ios.get(value);
    REQUIRE(ios.gcount() == 0);

    ios.clear();
    ios.seekg(0);
    REQUIRE(ios.tellg() == 0);

    for (size_t i = 0U; i < buffer1_size; ++i)
    {
      ios.get(value);
      REQUIRE(static_cast<std::byte>(value) == buffer1.at(i));
      REQUIRE(ios.gcount() == 1);
      REQUIRE(ios.tellg() == static_cast<int64_t>(i + 1U));
    }
    for (size_t i = 0U; i < buffer2_size; ++i)
    {
      ios.get(value);
      REQUIRE(static_cast<std::byte>(value) == buffer2.at(i));
      REQUIRE(ios.gcount() == 1);
      REQUIRE(ios.tellg() == static_cast<int64_t>(buffer1_size + i + 1U));
    }
    for (size_t i = 0U; i < buffer3_size; ++i)
    {
      ios.get(value);
      REQUIRE(static_cast<std::byte>(value) == buffer3.at(i));
      REQUIRE(ios.gcount() == 1);
      REQUIRE(ios.tellg() == static_cast<int64_t>(buffer1_size + buffer2_size + i + 1U));
    }
  }
}

} // namespace
} // namespace clockwork_logging::offboard
