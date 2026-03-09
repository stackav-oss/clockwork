// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/hash/sha1.hh"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>

#include <iomanip>
#include <sstream>
#include <string>
#include <utility>

namespace jewels::hash
{
namespace
{
std::string to_string(const Sha1::Digest& digest)
{
  std::ostringstream result;
  for (const auto& word : digest)
  {
    result << std::hex << std::setfill('0') << std::setw(8) << word;
  }
  return result.str();
}

template <typename... Source>
std::string sha1(Source&&... inputs)
{
  return to_string(Sha1(std::forward<Source>(inputs)...).get());
}

void multicut_test(std::span<const std::byte> blob, std::string_view expected)
{
  for (size_t cut1 = 0; cut1 <= blob.size(); cut1++)
  {
    for (size_t cut2 = cut1; cut2 <= blob.size(); cut2++)
    {
      INFO(fmt::format("multicut_test size={}, cut1={}, cut2={}", blob.size(), cut1, cut2));
      Sha1 hasher;
      hasher.update(blob.subspan(0, cut1));
      hasher.update(blob.subspan(cut1, cut2 - cut1));
      hasher.update(blob.subspan(cut2));
      CHECK(to_string(hasher.get()) == expected);
    }
  }
}

} // namespace

TEST_CASE("sha1")
{
  CHECK(to_string(Sha1().get()) == "da39a3ee5e6b4b0d3255bfef95601890afd80709");
  CHECK(to_string(Sha1("a", "b").get()) == "da23614e02469a0d7c7bd1bdab5c9c474b1904dc");
  CHECK(to_string(Sha1(std::array<uint8_t, 4>{1, 2, 3, 4}, "b").get()) == "65491638fde6c50cdb79166dd9f3c632a0add1ff");

  {
    Sha1 hasher("a", "b");
    hasher.update("q");
    CHECK(to_string(hasher.get()) == "75a2b2dd49ec3683a285eaf08c92977a2269925f");
    hasher.update("q");
    CHECK(to_string(hasher.get()) == "ff2ed7798038f9eff7529116f19d1438236e7f38");
  }

  const std::string_view blob_str = "Em2KICA9q3KzM78Mh2YQQmbUyATegAROfUXANSBILH5s82nTed91MwVNtHMepxXhQ4olZqFPw0HhgzYCPx"
                                    "nDogSdKAukEwiewI8Ygr0o0WCr4FfShxy7EtMCMmFPMJFqovgG2wotRkXHoKQ20TvHkGpFgsSx7i5tPwDq"
                                    "ufwAoOnCvs154lTE3G8tluWEldd2xP0miqr2";
  const auto blob = std::as_bytes(std::span(blob_str));

  multicut_test(blob, "e3d83df89e8ea1c082ae8b5ef7f834fb96322bb9");
  multicut_test(blob.subspan(0, 63), "a2be92d07949ae2d02d7c65f35256de43af1c838");
  multicut_test(blob.subspan(0, 64), "5337cbb1e86e8da821834bfd4a66f6085ce49b75");
  multicut_test(blob.subspan(0, 65), "bcf884fa475354883dc83b88da749cadb67694b2");
  multicut_test(blob.subspan(0, 127), "b1e0a6e5d82500e8bde10fdaedc98581ffedd0d5");
  multicut_test(blob.subspan(0, 128), "e67d24054b5fa8c65619d29c50267a6913605fea");
  multicut_test(blob.subspan(0, 129), "0d5f6edd93d4bf3a8617092ed5247ecc03fdcbc3");
}

} // namespace jewels::hash
