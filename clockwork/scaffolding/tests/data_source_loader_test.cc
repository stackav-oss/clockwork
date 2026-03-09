// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/scaffolding/data_source_loader.hh"
#include "clockwork/tags.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/container/compare.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/filesystem/file.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/testing/tmp_directory_guard.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <fcntl.h>
#include <functional>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <unistd.h>
#include <utility>
#include <vector>

namespace clockwork::scaffolding
{

namespace
{

void write_file(const jewels::filesystem::Directory& dir, std::string_view name, std::span<const std::byte> data)
{
  const jewels::filesystem::File file{dir.descriptor(), name, O_CREAT | O_WRONLY};
  CHECK(::write(file.descriptor(), data.data(), data.size()) == static_cast<ssize_t>(data.size()));
}

TEST_CASE("load_data_from_source - default construct sentinel")
{
  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};
  FirstMessageCache cache(memres);
  std::vector<Tappy<common::DataSource<>>> data_sources;

  DataSourceLoadResult result{memres};
  auto outcome = load_data_from_source(
    jewels::Out{result}, common::default_construct_data_source_sentinel, data_sources, memres, cache, "test_instance");

  REQUIRE(jewels::ok(outcome));
  CHECK(result.should_default_construct);
  CHECK(result.data.empty());
}

TEST_CASE("load_data_from_source - no fallback sentinel")
{
  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};
  FirstMessageCache cache(memres);
  std::vector<Tappy<common::DataSource<>>> data_sources;

  DataSourceLoadResult result{memres};
  auto outcome = load_data_from_source(
    jewels::Out{result}, common::no_fallback_data_source_sentinel, data_sources, memres, cache, "test_instance");

  REQUIRE(jewels::fails(outcome));
}

TEST_CASE("load_data_from_source - invalid index")
{
  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};
  FirstMessageCache cache(memres);
  std::vector<Tappy<common::DataSource<>>> data_sources;

  DataSourceLoadResult result{memres};
  auto outcome = load_data_from_source(jewels::Out{result}, 42U, data_sources, memres, cache, "test_instance");

  REQUIRE(jewels::fails(outcome));
}

TEST_CASE("load_data_from_source - load from file")
{
  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};
  FirstMessageCache cache(memres);

  const jewels::testing::TmpDirectoryGuard tmpdir_obj;
  const jewels::filesystem::Directory tmpdir{tmpdir_obj.get_path().c_str()};

  constexpr std::string_view test_data = "test file contents";
  const auto test_data_bytes = std::as_bytes(std::span(test_data));
  const std::string test_filename = "test_data.dat";
  write_file(tmpdir, test_filename, test_data_bytes);

  const auto representation_id = jewels::Uuid<RepresentationTag>::random_uuid();

  std::vector<Tappy<common::DataSource<>>> data_sources = {TapInit<Tachyon<common::DataSource<4096>>>{
    .representation_id = representation_id,
    .data_source_type = common::DataSourceType::file,
    .source_path_or_name = jewels::tap::VarString<4096>{""},
    .fallback_source = common::no_fallback_data_source_sentinel}};
  CHECK(data_sources[0].get_underlying_source_path_or_name().try_set((tmpdir_obj.get_path() / test_filename).string()));

  DataSourceLoadResult result{memres};
  auto outcome = load_data_from_source(jewels::Out{result}, 0U, data_sources, memres, cache, "test_instance");

  REQUIRE(jewels::ok(outcome));
  CHECK_FALSE(result.should_default_construct);
  CHECK(result.representation_id == representation_id);
  REQUIRE(result.data.size() == test_data.size());
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) Needed to verify contents
  CHECK(std::string_view(reinterpret_cast<const char*>(result.data.data()), result.data.size()) == test_data);
}

TEST_CASE("load_data_from_source - file not found with fallback to default construct")
{
  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};
  FirstMessageCache cache(memres);

  const auto primary_repr_id = jewels::Uuid<RepresentationTag>::random_uuid();
  const auto fallback_repr_id = jewels::Uuid<RepresentationTag>::random_uuid();

  std::vector<Tappy<common::DataSource<>>> data_sources = {
    TapInit<Tachyon<common::DataSource<4096>>>{
      .representation_id = primary_repr_id,
      .data_source_type = common::DataSourceType::file,
      .source_path_or_name = jewels::tap::VarString<4096>{"/nonexistent/file.dat"},
      .fallback_source = 1U},
    TapInit<Tachyon<common::DataSource<4096>>>{
      .representation_id = fallback_repr_id,
      .data_source_type = common::DataSourceType::file,
      .source_path_or_name = jewels::tap::VarString<4096>{""},
      .fallback_source = common::default_construct_data_source_sentinel}};

  DataSourceLoadResult result{memres};
  auto outcome = load_data_from_source(jewels::Out{result}, 0U, data_sources, memres, cache, "test_instance");

  REQUIRE(jewels::ok(outcome));
  CHECK(result.should_default_construct);
  CHECK(result.data.empty());
}

TEST_CASE("load_data_from_source - file not found with no fallback")
{
  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};
  FirstMessageCache cache(memres);

  const auto repr_id = jewels::Uuid<RepresentationTag>::random_uuid();

  std::vector<Tappy<common::DataSource<>>> data_sources = {TapInit<Tachyon<common::DataSource<4096>>>{
    .representation_id = repr_id,
    .data_source_type = common::DataSourceType::file,
    .source_path_or_name = jewels::tap::VarString<4096>{"/nonexistent/file.dat"},
    .fallback_source = common::no_fallback_data_source_sentinel}};

  DataSourceLoadResult result{memres};
  auto outcome = load_data_from_source(jewels::Out{result}, 0U, data_sources, memres, cache, "test_instance");

  REQUIRE(jewels::fails(outcome));
}

TEST_CASE("load_data_from_source - load from log cache")
{
  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};

  // Populate cache with a test message
  FirstMessageCache cache(memres);
  const std::string_view channel_name = "/test/channel";
  const std::string_view test_data = "message from log";
  std::pmr::vector<std::byte> test_message(memres);
  const auto test_data_bytes = std::as_bytes(std::span(test_data));
  test_message.assign(test_data_bytes.begin(), test_data_bytes.end());
  cache.emplace(channel_name, std::move(test_message));

  const auto representation_id = jewels::Uuid<RepresentationTag>::random_uuid();

  std::vector<Tappy<common::DataSource<>>> data_sources = {TapInit<Tachyon<common::DataSource<4096>>>{
    .representation_id = representation_id,
    .data_source_type = common::DataSourceType::log_first_message,
    .source_path_or_name = jewels::tap::VarString<4096>{""},
    .fallback_source = common::no_fallback_data_source_sentinel}};
  CHECK(data_sources[0].get_underlying_source_path_or_name().try_set(channel_name));
  DataSourceLoadResult result{memres};
  auto outcome = load_data_from_source(jewels::Out{result}, 0U, data_sources, memres, cache, "test_instance");

  REQUIRE(jewels::ok(outcome));
  CHECK_FALSE(result.should_default_construct);
  CHECK(result.representation_id == representation_id);
  REQUIRE(result.data.size() == test_data.size());
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) Needed to verify contents
  CHECK(std::string_view(reinterpret_cast<const char*>(result.data.data()), result.data.size()) == test_data);
}

TEST_CASE("load_data_from_source - log channel not in cache with fallback")
{
  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};
  FirstMessageCache cache(memres);

  const auto primary_repr_id = jewels::Uuid<RepresentationTag>::random_uuid();
  const auto fallback_repr_id = jewels::Uuid<RepresentationTag>::random_uuid();

  std::vector<Tappy<common::DataSource<>>> data_sources = {
    TapInit<Tachyon<common::DataSource<4096>>>{
      .representation_id = primary_repr_id,
      .data_source_type = common::DataSourceType::log_first_message,
      .source_path_or_name = jewels::tap::VarString<4096>{""},
      .fallback_source = 1U},
    TapInit<Tachyon<common::DataSource<4096>>>{
      .representation_id = fallback_repr_id,
      .data_source_type = common::DataSourceType::file,
      .source_path_or_name = jewels::tap::VarString<4096>{""},
      .fallback_source = common::default_construct_data_source_sentinel}};
  CHECK(data_sources[0].get_underlying_source_path_or_name().try_set("/missing/channel"));
  DataSourceLoadResult result{memres};
  auto outcome = load_data_from_source(jewels::Out{result}, 0U, data_sources, memres, cache, "test_instance");

  REQUIRE(jewels::ok(outcome));
  CHECK(result.should_default_construct);
}

TEST_CASE("load_data_from_source - cycle detection")
{
  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};
  FirstMessageCache cache(memres);

  const auto repr_id = jewels::Uuid<RepresentationTag>::random_uuid();

  // Create a cycle: 0 -> 1 -> 0
  std::vector<Tappy<common::DataSource<>>> data_sources = {
    TapInit<Tachyon<common::DataSource<4096>>>{
      .representation_id = repr_id,
      .data_source_type = common::DataSourceType::file,
      .source_path_or_name = jewels::tap::VarString<4096>{"/nonexistent1.dat"},
      .fallback_source = 1U},
    TapInit<Tachyon<common::DataSource<4096>>>{
      .representation_id = repr_id,
      .data_source_type = common::DataSourceType::file,
      .source_path_or_name = jewels::tap::VarString<4096>{"/nonexistent2.dat"},
      .fallback_source = 0U}};

  DataSourceLoadResult result{memres};
  auto outcome = load_data_from_source(jewels::Out{result}, 0U, data_sources, memres, cache, "test_instance");
  REQUIRE(jewels::fails(outcome));
}

TEST_CASE("load_data_from_source - chain of fallbacks")
{
  const jewels::memory::MemoryResource memres{std::pmr::new_delete_resource()};
  FirstMessageCache cache(memres);

  const jewels::testing::TmpDirectoryGuard tmpdir_obj;
  const jewels::filesystem::Directory tmpdir{tmpdir_obj.get_path().c_str()};

  constexpr std::string_view test_data = "final fallback data";
  const auto test_data_bytes = std::as_bytes(std::span(test_data));
  const std::string test_filename = "fallback3.dat";
  write_file(tmpdir, test_filename, test_data_bytes);

  const auto repr_id = jewels::Uuid<RepresentationTag>::random_uuid();

  // Chain: 0 (missing file) -> 1 (missing log) -> 2 (missing file) -> 3 (valid file)
  std::vector<Tappy<common::DataSource<>>> data_sources = {
    TapInit<Tachyon<common::DataSource<4096>>>{
      .representation_id = repr_id,
      .data_source_type = common::DataSourceType::file,
      .source_path_or_name = jewels::tap::VarString<4096>{""},
      .fallback_source = 1U},
    TapInit<Tachyon<common::DataSource<4096>>>{
      .representation_id = repr_id,
      .data_source_type = common::DataSourceType::log_first_message,
      .source_path_or_name = jewels::tap::VarString<4096>{""},
      .fallback_source = 2U},
    TapInit<Tachyon<common::DataSource<4096>>>{
      .representation_id = repr_id,
      .data_source_type = common::DataSourceType::file,
      .source_path_or_name = jewels::tap::VarString<4096>{""},
      .fallback_source = 3U},
    TapInit<Tachyon<common::DataSource<4096>>>{
      .representation_id = repr_id,
      .data_source_type = common::DataSourceType::file,
      .source_path_or_name = jewels::tap::VarString<4096>{""},
      .fallback_source = common::no_fallback_data_source_sentinel}};
  CHECK(data_sources[0].get_underlying_source_path_or_name().try_set("/nonexistent0.dat"));
  CHECK(data_sources[1].get_underlying_source_path_or_name().try_set("/missing/channel"));
  CHECK(data_sources[2].get_underlying_source_path_or_name().try_set("/nonexistent2.dat"));
  CHECK(data_sources[3].get_underlying_source_path_or_name().try_set((tmpdir_obj.get_path() / test_filename).string()));

  DataSourceLoadResult result{memres};
  auto outcome = load_data_from_source(jewels::Out{result}, 0U, data_sources, memres, cache, "test_instance");

  REQUIRE(jewels::ok(outcome));
  CHECK_FALSE(result.should_default_construct);
  REQUIRE(result.data.size() == test_data.size());
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) Needed to verify contents
  CHECK(std::string_view(reinterpret_cast<const char*>(result.data.data()), result.data.size()) == test_data);
}

} // namespace

} // namespace clockwork::scaffolding
