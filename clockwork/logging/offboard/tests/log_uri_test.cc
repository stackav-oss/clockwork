// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/offboard/log_uri.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <memory_resource>
#include <sstream> // IWYU pragma: keep
#include <string>
#include <string_view>
#include <utility>

namespace clockwork_logging::offboard
{
namespace
{

TEST_CASE("try_make")
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};

  SECTION("file, no prefix")
  {
    SECTION("Valid path")
    {
      const auto make_result = LogUri::try_make("/foo/bar.baz", memory_resource);
      REQUIRE(make_result);
      const auto& log_uri = make_result.value();
      REQUIRE(log_uri.scheme() == LogUriScheme::file);
      REQUIRE(log_uri.host().empty());
      REQUIRE(log_uri.path() == "/foo/bar.baz");
      REQUIRE(log_uri == "/foo/bar.baz");
      REQUIRE(log_uri == "file:/foo/bar.baz");
      REQUIRE(log_uri.string() == "/foo/bar.baz");
      std::stringstream sstream;
      sstream << log_uri; // NOLINT(cert-err33-c) False positive
      CHECK(sstream.str() == "/foo/bar.baz");
    }

    SECTION("Invalid path")
    {
      REQUIRE_FALSE(LogUri::try_make("", memory_resource));
    }
  }

  SECTION("file")
  {
    SECTION("Valid path")
    {
      const auto make_result = LogUri::try_make("file:/foo/bar.baz", memory_resource);
      REQUIRE(make_result);
      const auto& log_uri = make_result.value();
      REQUIRE(log_uri.scheme() == LogUriScheme::file);
      REQUIRE(log_uri.host().empty());
      REQUIRE(log_uri.path() == "/foo/bar.baz");
      REQUIRE(log_uri == "file:/foo/bar.baz");
      REQUIRE(log_uri == "/foo/bar.baz");
      REQUIRE(log_uri.string() == "file:/foo/bar.baz");
      std::stringstream sstream;
      sstream << log_uri; // NOLINT(cert-err33-c) False positive
      CHECK(sstream.str() == "file:/foo/bar.baz");
    }

    SECTION("Invalid path")
    {
      REQUIRE_FALSE(LogUri::try_make("file:", memory_resource));
    }
  }

  SECTION("s3")
  {
    SECTION("Valid path")
    {
      const auto make_result = LogUri::try_make("s3://bucket/foo/bar.baz", memory_resource);
      REQUIRE(make_result);
      const auto& log_uri = make_result.value();
      REQUIRE(log_uri.scheme() == LogUriScheme::s3);
      REQUIRE(log_uri.host() == "bucket");
      REQUIRE(log_uri.path() == "/foo/bar.baz");
      REQUIRE(log_uri == "s3://bucket/foo/bar.baz");
      REQUIRE(log_uri.string() == "s3://bucket/foo/bar.baz");
      std::stringstream sstream;
      sstream << log_uri; // NOLINT(cert-err33-c) False positive
      CHECK(sstream.str() == "s3://bucket/foo/bar.baz");
    }

    SECTION("Invalid path")
    {
      REQUIRE_FALSE(LogUri::try_make("s3:/foo/bar.baz", memory_resource));
      REQUIRE_FALSE(LogUri::try_make("s3:///", memory_resource));
      REQUIRE_FALSE(LogUri::try_make("s3:///foo/bar.baz", memory_resource));
      REQUIRE_FALSE(LogUri::try_make("s3://", memory_resource));
      REQUIRE_FALSE(LogUri::try_make("s3://bucket", memory_resource));
    }
  }
}

TEST_CASE("Comparison")
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};

  const auto [uri_str1, uri_str2] = GENERATE(
    std::make_pair("file:/foo/bar.baz", "s3://bucket/foo/bar.baz"),
    std::make_pair("file:/foo1/bar.baz", "file:/foo2/bar.baz"),
    std::make_pair("/foo1/bar.baz", "file:/foo2/bar.baz"),
    std::make_pair("file:/foo1/bar.baz", "/foo2/bar.baz"),
    std::make_pair("/foo1/bar.baz", "/foo2/bar.baz"),
    std::make_pair("s3://bucket1/foo/bar.baz", "s3://bucket2/foo/bar.baz"),
    std::make_pair("s3://bucket/foo1/bar.baz", "s3://bucket/foo2/bar.baz"));

  CAPTURE(uri_str1);
  CAPTURE(uri_str2);

  SECTION("Compare two LogUris")
  {
    const auto log_uri1 = LogUri::try_make(uri_str1, memory_resource).value();
    const auto log_uri1_2 = LogUri::try_make(uri_str1, memory_resource).value();
    const auto log_uri2 = LogUri::try_make(uri_str2, memory_resource).value();
    const auto log_uri2_2 = LogUri::try_make(uri_str2, memory_resource).value();

    REQUIRE(log_uri1 == log_uri1_2);
    REQUIRE_FALSE(log_uri1 == log_uri2_2);
    REQUIRE_FALSE(log_uri2 == log_uri1_2);
    REQUIRE(log_uri2 == log_uri2_2);

    REQUIRE_FALSE(log_uri1 != log_uri1_2);
    REQUIRE(log_uri1 != log_uri2_2);
    REQUIRE(log_uri2 != log_uri1_2);
    REQUIRE_FALSE(log_uri2 != log_uri2_2);

    REQUIRE_FALSE(log_uri1 < log_uri1_2);
    REQUIRE(log_uri1 < log_uri2_2);
    REQUIRE_FALSE(log_uri2 < log_uri1_2);
    REQUIRE_FALSE(log_uri2 < log_uri2_2);

    REQUIRE(log_uri1 <= log_uri1_2);
    REQUIRE(log_uri1 <= log_uri2_2);
    REQUIRE_FALSE(log_uri2 <= log_uri1_2);
    REQUIRE(log_uri2 <= log_uri2_2);

    REQUIRE_FALSE(log_uri1 > log_uri1_2);
    REQUIRE_FALSE(log_uri1 > log_uri2_2);
    REQUIRE(log_uri2 > log_uri1_2);
    REQUIRE_FALSE(log_uri2 > log_uri2_2);

    REQUIRE(log_uri1 >= log_uri1_2);
    REQUIRE_FALSE(log_uri1 >= log_uri2_2);
    REQUIRE(log_uri2 >= log_uri1_2);
    REQUIRE(log_uri2 >= log_uri2_2);
  }

  SECTION("Compare with string")
  {
    const auto log_uri1 = LogUri::try_make(uri_str1, memory_resource).value();
    const auto log_uri2 = LogUri::try_make(uri_str2, memory_resource).value();

    REQUIRE(log_uri1 == uri_str1);
    REQUIRE_FALSE(log_uri1 == uri_str2);
    REQUIRE_FALSE(log_uri2 == uri_str1);
    REQUIRE(log_uri2 == uri_str2);

    REQUIRE_FALSE(log_uri1 != uri_str1);
    REQUIRE(log_uri1 != uri_str2);
    REQUIRE(log_uri2 != uri_str1);
    REQUIRE_FALSE(log_uri2 != uri_str2);

    REQUIRE_FALSE(log_uri1 == "invalid_uri");
    REQUIRE(log_uri1 != "invalid_uri");
  }
}

TEST_CASE("Path operations")
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};

  auto make_result = LogUri::try_make("s3://bucket/foo/bar", memory_resource);
  REQUIRE(make_result);
  auto log_uri = make_result.value();

  SECTION("parent_uri")
  {
    log_uri = log_uri.parent_uri();
    REQUIRE(log_uri == "s3://bucket/foo");
    log_uri = log_uri.parent_uri();
    REQUIRE(log_uri == "s3://bucket/");
    log_uri = log_uri.parent_uri();
    REQUIRE(log_uri == "s3://bucket/");
  }

  SECTION("filename")
  {
    REQUIRE(log_uri.has_filename());
    REQUIRE(log_uri.filename() == "bar");
    log_uri += "/";
    REQUIRE(log_uri == "s3://bucket/foo/bar/");
    REQUIRE_FALSE(log_uri.has_filename());
    REQUIRE(log_uri.filename().empty());
    log_uri /= "bar2.baz";
    REQUIRE(log_uri == "s3://bucket/foo/bar/bar2.baz");
    REQUIRE(log_uri.has_filename());
    REQUIRE(log_uri.filename() == "bar2.baz");
    log_uri.replace_filename("bar3.baz"); // NOLINT(cert-err33-c) False positive
    REQUIRE(log_uri == "s3://bucket/foo/bar/bar3.baz");
    REQUIRE(log_uri.has_filename());
    REQUIRE(log_uri.filename() == "bar3.baz");
    log_uri.remove_filename(); // NOLINT(cert-err33-c) False positive
    REQUIRE(log_uri == "s3://bucket/foo/bar/");
    REQUIRE_FALSE(log_uri.has_filename());
    REQUIRE(log_uri.filename().empty());
    log_uri.remove_filename(); // NOLINT(cert-err33-c) False positive
    REQUIRE(log_uri == "s3://bucket/foo/bar/");
    REQUIRE_FALSE(log_uri.has_filename());
    REQUIRE(log_uri.filename().empty());
  }

  SECTION("extension")
  {
    REQUIRE_FALSE(log_uri.has_extension());
    REQUIRE(log_uri.extension().empty());
    log_uri.replace_extension(".baz"); // NOLINT(cert-err33-c) False positive
    REQUIRE(log_uri.has_extension());
    REQUIRE(log_uri.extension() == ".baz");
    log_uri.replace_extension(".baz2"); // NOLINT(cert-err33-c) False positive
    REQUIRE(log_uri.has_extension());
    REQUIRE(log_uri.extension() == ".baz2");
  }

  SECTION("stem")
  {
    REQUIRE(log_uri.has_stem());
    REQUIRE(log_uri.stem() == "bar");
    log_uri += "/";
    REQUIRE(log_uri == "s3://bucket/foo/bar/");
    REQUIRE_FALSE(log_uri.has_stem());
    REQUIRE(log_uri.stem().empty());
    log_uri /= "bar2.baz";
    REQUIRE(log_uri == "s3://bucket/foo/bar/bar2.baz");
    REQUIRE(log_uri.has_stem());
    REQUIRE(log_uri.stem() == "bar2");
  }

  SECTION("apply_relative_path")
  {
    REQUIRE(log_uri.apply_relative_path("") == "s3://bucket/foo/bar");
    REQUIRE(log_uri.apply_relative_path("./.") == "s3://bucket/foo/bar");
    REQUIRE(log_uri.apply_relative_path("..") == "s3://bucket/foo");
    REQUIRE(log_uri.apply_relative_path("../.") == "s3://bucket/foo");
    REQUIRE(log_uri.apply_relative_path("../.") == "s3://bucket/foo");
    REQUIRE(log_uri.apply_relative_path("./..") == "s3://bucket/foo");
    REQUIRE(log_uri.apply_relative_path("../..") == "s3://bucket/");
    REQUIRE(log_uri.apply_relative_path("bar2/bar3.baz") == "s3://bucket/foo/bar/bar2/bar3.baz");
    REQUIRE(log_uri.apply_relative_path("../bar2/bar3.baz") == "s3://bucket/foo/bar2/bar3.baz");
    REQUIRE(log_uri.apply_relative_path("../../bar2/bar3.baz") == "s3://bucket/bar2/bar3.baz");
    REQUIRE(log_uri.apply_relative_path("../../../bar2/bar3.baz") == "s3://bucket/bar2/bar3.baz");
    REQUIRE(log_uri.apply_relative_path("./bar2/bar3.baz") == "s3://bucket/foo/bar/bar2/bar3.baz");
    REQUIRE(log_uri.apply_relative_path("./.././bar2/bar3.baz") == "s3://bucket/foo/bar2/bar3.baz");
    REQUIRE(log_uri.apply_relative_path("./../.././bar2/bar3.baz") == "s3://bucket/bar2/bar3.baz");
    REQUIRE(log_uri.apply_relative_path("../.././../bar2/bar3.baz") == "s3://bucket/bar2/bar3.baz");
  }
}

TEST_CASE("File URI relative path")
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};

  auto make_result = LogUri::try_make("file:foo/bar/", memory_resource);
  REQUIRE(make_result);
  auto log_uri = make_result.value();

  REQUIRE(log_uri.apply_relative_path("") == "file:foo/bar");
  REQUIRE(log_uri.apply_relative_path("./.") == "file:foo/bar");
  REQUIRE(log_uri.apply_relative_path("..") == "file:foo");
  REQUIRE(log_uri.apply_relative_path("../.") == "file:foo");
  REQUIRE(log_uri.apply_relative_path("../.") == "file:foo");
  REQUIRE(log_uri.apply_relative_path("./..") == "file:foo");
  REQUIRE(log_uri.apply_relative_path("../..") == "file:.");
  REQUIRE(log_uri.apply_relative_path("../../..") == "file:..");
  REQUIRE(log_uri.apply_relative_path("bar2/bar3.baz") == "file:foo/bar/bar2/bar3.baz");
  REQUIRE(log_uri.apply_relative_path("../bar2/bar3.baz") == "file:foo/bar2/bar3.baz");
  REQUIRE(log_uri.apply_relative_path("../../bar2/bar3.baz") == "file:bar2/bar3.baz");
  REQUIRE(log_uri.apply_relative_path("../../../bar2/bar3.baz") == "file:../bar2/bar3.baz");
  REQUIRE(log_uri.apply_relative_path("./bar2/bar3.baz") == "file:foo/bar/bar2/bar3.baz");
  REQUIRE(log_uri.apply_relative_path("./.././bar2/bar3.baz") == "file:foo/bar2/bar3.baz");
  REQUIRE(log_uri.apply_relative_path("./../.././bar2/bar3.baz") == "file:bar2/bar3.baz");
  REQUIRE(log_uri.apply_relative_path("../.././../bar2/bar3.baz") == "file:../bar2/bar3.baz");
}

TEST_CASE("Regular file no scheme relative path")
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};

  auto make_result = LogUri::try_make("foo/bar/", memory_resource);
  REQUIRE(make_result);
  auto log_uri = make_result.value();

  REQUIRE(log_uri.apply_relative_path("") == "foo/bar");
  REQUIRE(log_uri.apply_relative_path("./.") == "foo/bar");
  REQUIRE(log_uri.apply_relative_path("..") == "foo");
  REQUIRE(log_uri.apply_relative_path("../.") == "foo");
  REQUIRE(log_uri.apply_relative_path("../.") == "foo");
  REQUIRE(log_uri.apply_relative_path("./..") == "foo");
  REQUIRE(log_uri.apply_relative_path("../..") == ".");
  REQUIRE(log_uri.apply_relative_path("../../..") == "..");
  REQUIRE(log_uri.apply_relative_path("bar2/bar3.baz") == "foo/bar/bar2/bar3.baz");
  REQUIRE(log_uri.apply_relative_path("../bar2/bar3.baz") == "foo/bar2/bar3.baz");
  REQUIRE(log_uri.apply_relative_path("../../bar2/bar3.baz") == "bar2/bar3.baz");
  REQUIRE(log_uri.apply_relative_path("../../../bar2/bar3.baz") == "../bar2/bar3.baz");
  REQUIRE(log_uri.apply_relative_path("./bar2/bar3.baz") == "foo/bar/bar2/bar3.baz");
  REQUIRE(log_uri.apply_relative_path("./.././bar2/bar3.baz") == "foo/bar2/bar3.baz");
  REQUIRE(log_uri.apply_relative_path("./../.././bar2/bar3.baz") == "bar2/bar3.baz");
  REQUIRE(log_uri.apply_relative_path("../.././../bar2/bar3.baz") == "../bar2/bar3.baz");
}

} // namespace
} // namespace clockwork_logging::offboard
