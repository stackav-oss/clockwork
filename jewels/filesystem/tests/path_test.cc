// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/filesystem/path.hh"
#include "jewels/memory/memory_resource.hh"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <filesystem>
#include <memory_resource>
#include <string>
#include <string_view>
#include <utility>

namespace jewels::filesystem::testing
{
namespace
{

TEST_CASE("Path")
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};

  SECTION("Constructor, empty, string, c_str")
  {
    const Path empty_path{memory_resource};
    REQUIRE(empty_path.empty());
    REQUIRE(empty_path.string().empty());
    REQUIRE(std::string_view{empty_path.c_str()}.empty());

    const Path test_path{"test", memory_resource};
    REQUIRE_FALSE(test_path.empty());
    REQUIRE(test_path.string() == "test");
    REQUIRE(std::string_view{test_path.c_str()} == "test");
  }

  SECTION("Copy constructor, copy assignment")
  {
    const Path path1{"test", memory_resource};
    const Path path2{"test", memory_resource};
    REQUIRE(path2.string() == path1.string());
    Path path3{memory_resource};
    path3 = path1;
    REQUIRE(path3.string() == path1.string());
  }

  SECTION("Parent path")
  {
    const auto* const path =
      GENERATE("/var/tmp/example.txt", "/var/tmp//example.txt", "/", "//", "/var", "/var/tmp/.", "var_tmp.txt");
    CAPTURE(path);
    REQUIRE(
      Path{path, memory_resource}.parent_path().string_view() == std::filesystem::path(path).parent_path().string());
    REQUIRE(Path{path, memory_resource}.has_parent_path() == std::filesystem::path(path).has_parent_path());
  }

  SECTION("File name")
  {
    const auto* const path =
      GENERATE("/foo/bar.txt", "/foo/.bar", "/foo/bar/", "/foo/.", "/foo/..", ".", "..", "/", "//", "//host");
    CAPTURE(path);
    REQUIRE(Path{path, memory_resource}.filename().string_view() == std::filesystem::path(path).filename().string());
    REQUIRE(Path{path, memory_resource}.has_filename() == std::filesystem::path(path).has_filename());
  }

  SECTION("Stem")
  {
    const auto* const path = GENERATE("/foo/bar.txt", "/foo/.bar", "foo.bar.baz.tar", "/foo/.", "/foo/..", "/foo/bar/");
    CAPTURE(path);
    REQUIRE(Path{path, memory_resource}.stem().string_view() == std::filesystem::path(path).stem().string());
    REQUIRE(Path{path, memory_resource}.has_stem() == std::filesystem::path(path).has_stem());
  }

  SECTION("Absolute")
  {
    const auto* const path = GENERATE("/foo/bar.txt", "/foo/.bar", "foo.bar.baz.tar", "/foo/.", "/foo/..", "/foo/bar/");
    CAPTURE(path);
    REQUIRE(Path{path, memory_resource}.is_absolute() == std::filesystem::path(path).is_absolute());
  }

  SECTION("Extension")
  {
    const auto* const path = GENERATE(
      "/foo/bar.txt",
      "/foo/bar.",
      "/foo/.bar",
      "/foo/bar",
      "/foo/bar.txt/bar.cc",
      "/foo/bar.txt/bar.",
      "/foo/bar.txt/bar",
      "/foo/.",
      "/foo/..",
      "/foo/.hidden",
      "/foo/..bar",
      "/foo/.",
      "/foo/..");
    CAPTURE(path);
    REQUIRE(Path{path, memory_resource}.extension().string_view() == std::filesystem::path(path).extension().string());
    REQUIRE(Path{path, memory_resource}.has_extension() == std::filesystem::path(path).has_extension());
  }

  SECTION("Clear")
  {
    Path path{"1", memory_resource};
    REQUIRE_FALSE(path.empty());
    path.clear();
    REQUIRE(path.empty());
  }

  SECTION("Remove filename")
  {
    const auto* path = GENERATE("foo/bar", "foo/", "/foo", "/foo/.", "/foo/..", "/", "", "foo");
    CAPTURE(path);
    REQUIRE(
      Path{path, memory_resource}.remove_filename().string_view() ==
      std::filesystem::path(path).remove_filename().string());
  }

  SECTION("Replace filename")
  {
    const auto [path, filename] =
      GENERATE(std::make_pair("/foo", "bar"), std::make_pair("/", "bar"), std::make_pair("", "pub"));
    CAPTURE(path, filename);
    REQUIRE(
      Path{path, memory_resource}.replace_filename(filename).string_view() ==
      std::filesystem::path(path).replace_filename(filename).string());
  }

  SECTION("Replace extension")
  {
    const auto [path, extension] = GENERATE(
      std::make_pair("/foo/bar.jpg", ".png"),
      std::make_pair("/foo/bar.jpg", "png"),
      std::make_pair("/foo/bar.jpg", "."),
      std::make_pair("/foo/bar.jpg", ""),
      std::make_pair("/foo/bar.", "png"),
      std::make_pair("/foo/bar", ".png"),
      std::make_pair("/foo/bar", "png"),
      std::make_pair("/foo/bar", "."),
      std::make_pair("/foo/bar", ""),
      std::make_pair("/foo/.", ".png"),
      std::make_pair("/foo/.", "png"),
      std::make_pair("/foo/.", "."),
      std::make_pair("/foo/.", ""),
      std::make_pair("/foo/", ".png"),
      std::make_pair("/foo/", "png"));
    CAPTURE(path, extension);
    REQUIRE(
      Path{path, memory_resource}.replace_extension(extension).string_view() ==
      std::filesystem::path(path).replace_extension(extension).string());
  }

  SECTION("+= operator")
  {
    Path path("aaa", memory_resource);
    path += "bbb";
    REQUIRE(path == "aaabbb");
  }

  SECTION("/= operator")
  {
    Path path{memory_resource};
    path /= "aaa";
    REQUIRE(path == "aaa");
    path /= "bbb/";
    REQUIRE(path == "aaa/bbb/");
    path /= "ccc";
    REQUIRE(path == "aaa/bbb/ccc");
    path /= "/ddd";
    REQUIRE(path == "/ddd");
  }

  SECTION("/ operator")
  {
    REQUIRE(Path{"aaa", memory_resource} / "bbb/" == "aaa/bbb/");
    REQUIRE(Path{"aaa", memory_resource} / "bbb/" / "ccc" == "aaa/bbb/ccc");
    REQUIRE(Path{"aaa", memory_resource} / "bbb/" / "ccc" / "/ddd" == "/ddd");
  }

  SECTION("Comparison")
  {
    const Path path1{"1", memory_resource};
    const Path path1_2{"1", memory_resource};
    const Path path2{"2", memory_resource};
    const Path path2_2{"2", memory_resource};

    REQUIRE(path1 == path1_2);
    REQUIRE_FALSE(path1 == path2_2);
    REQUIRE_FALSE(path2 == path1_2);
    REQUIRE(path2 == path2_2);

    REQUIRE_FALSE(path1 != path1_2);
    REQUIRE(path1 != path2_2);
    REQUIRE(path2 != path1_2);
    REQUIRE_FALSE(path2 != path2_2);

    REQUIRE_FALSE(path1 < path1_2);
    REQUIRE(path1 < path2_2);
    REQUIRE_FALSE(path2 < path1_2);
    REQUIRE_FALSE(path2 < path2_2);

    REQUIRE(path1 <= path1_2);
    REQUIRE(path1 <= path2_2);
    REQUIRE_FALSE(path2 <= path1_2);
    REQUIRE(path2 <= path2_2);

    REQUIRE_FALSE(path1 > path1_2);
    REQUIRE_FALSE(path1 > path2_2);
    REQUIRE(path2 > path1_2);
    REQUIRE_FALSE(path2 > path2_2);

    REQUIRE(path1 >= path1_2);
    REQUIRE_FALSE(path1 >= path2_2);
    REQUIRE(path2 >= path1_2);
    REQUIRE(path2 >= path2_2);
  }

  SECTION("Comparison with null terminated array of char")
  {
    const Path path1{"1", memory_resource};
    const auto* const path1_2 = "1";
    const Path path2{"2", memory_resource};
    const auto* const path2_2 = "2";

    REQUIRE(path1 == path1_2);
    REQUIRE_FALSE(path1 == path2_2);
    REQUIRE_FALSE(path2 == path1_2);
    REQUIRE(path2 == path2_2);

    REQUIRE_FALSE(path1 != path1_2);
    REQUIRE(path1 != path2_2);
    REQUIRE(path2 != path1_2);
    REQUIRE_FALSE(path2 != path2_2);

    REQUIRE_FALSE(path1 < path1_2);
    REQUIRE(path1 < path2_2);
    REQUIRE_FALSE(path2 < path1_2);
    REQUIRE_FALSE(path2 < path2_2);

    REQUIRE(path1 <= path1_2);
    REQUIRE(path1 <= path2_2);
    REQUIRE_FALSE(path2 <= path1_2);
    REQUIRE(path2 <= path2_2);

    REQUIRE_FALSE(path1 > path1_2);
    REQUIRE_FALSE(path1 > path2_2);
    REQUIRE(path2 > path1_2);
    REQUIRE_FALSE(path2 > path2_2);

    REQUIRE(path1 >= path1_2);
    REQUIRE_FALSE(path1 >= path2_2);
    REQUIRE(path2 >= path1_2);
    REQUIRE(path2 >= path2_2);
  }

  SECTION("Comparison with string_view (through implicit conversion)")
  {
    const Path path1{"1", memory_resource};
    const std::string_view path1_2{path1};
    const Path path2{"2", memory_resource};
    const std::string_view path2_2{path2};

    REQUIRE(path1 == path1_2);
    REQUIRE_FALSE(path1 == path2_2);
    REQUIRE_FALSE(path2 == path1_2);
    REQUIRE(path2 == path2_2);

    REQUIRE_FALSE(path1 != path1_2);
    REQUIRE(path1 != path2_2);
    REQUIRE(path2 != path1_2);
    REQUIRE_FALSE(path2 != path2_2);

    REQUIRE_FALSE(path1 < path1_2);
    REQUIRE(path1 < path2_2);
    REQUIRE_FALSE(path2 < path1_2);
    REQUIRE_FALSE(path2 < path2_2);

    REQUIRE(path1 <= path1_2);
    REQUIRE(path1 <= path2_2);
    REQUIRE_FALSE(path2 <= path1_2);
    REQUIRE(path2 <= path2_2);

    REQUIRE_FALSE(path1 > path1_2);
    REQUIRE_FALSE(path1 > path2_2);
    REQUIRE(path2 > path1_2);
    REQUIRE_FALSE(path2 > path2_2);

    REQUIRE(path1 >= path1_2);
    REQUIRE_FALSE(path1 >= path2_2);
    REQUIRE(path2 >= path1_2);
    REQUIRE(path2 >= path2_2);
  }
}

} // namespace
} // namespace jewels::filesystem::testing
