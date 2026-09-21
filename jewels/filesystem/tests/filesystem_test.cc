// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/filesystem/filesystem.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/testing/error_code_stringmakers.hh" // IWYU pragma: keep
#include "jewels/testing/expected_stringmakers.hh"   // IWYU pragma: keep
#include "jewels/testing/filesystem_wrapper.hh"
#include "jewels/testing/path_stringmakers.hh" // IWYU pragma: keep
#include "jewels/testing/tmp_directory_guard.hh"
#include "jewels/time/sync_time.hh"

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cerrno>
#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdlib>
#include <dirent.h>
#include <fcntl.h>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <sys/types.h>
#include <thread>
#include <type_traits>
#include <unistd.h>
#include <utility>
#include <vector>

namespace jewels::filesystem::testing
{
namespace
{

/// Create a test file
/// @tparam FilesystemType Filesystem type
/// @param[in] path File path
/// @param[in] data File data
/// @param[in,out] filesys Filesystem instance
/// @return Error code on failure
template <typename FilesystemType>
[[nodiscard]] jewels::expected<void, ErrorCode>
create_test_file(std::string_view path, std::span<const std::byte> data, FilesystemType& filesys)
{
  auto open_result = filesys.open(path, O_CREAT | O_EXCL | O_WRONLY);
  if (!open_result)
  {
    return jewels::unexpected(open_result.error());
  }
  if (const auto write_result = filesys.write(*open_result, data); !write_result)
  {
    return jewels::unexpected(write_result.error());
  }
  return open_result->close();
}

/// Read the contents of a test file
/// @tparam FilesystemType Filesystem type
/// @param[in] path File path
/// @param[in] data Expected data
/// @param[in,out] filesys Filesystem instance
/// @return File contents or error code on failure
template <typename FilesystemType>
[[nodiscard]] jewels::expected<std::string, ErrorCode> read_test_file(std::string_view path, FilesystemType& filesys)
{
  const auto open_result = filesys.open(path);
  if (!open_result)
  {
    return jewels::unexpected(open_result.error());
  }
  auto get_size_result = filesys.get_size(*open_result);
  if (!get_size_result)
  {
    return jewels::unexpected(open_result.error());
  }
  std::string read_str(*get_size_result, '\0');
  const auto read_result =
    filesys.read(*open_result, std::as_writable_bytes(std::span{read_str.data(), read_str.size()}));
  if (!read_result)
  {
    return jewels::unexpected(read_result.error());
  }
  return {std::move(read_str)};
}

TEMPLATE_TEST_CASE("Filesystem", "[filesystem]", Filesystem, FilesystemWrapper)
{
  const memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const jewels::testing::TmpDirectoryGuard test_dir(memory_resource);
  TestType filesys{memory_resource};
  filesys.set_verbosity(Filesystem::ErrorVerbosity::verbose);

  constexpr std::string_view test_data = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

  using namespace std::literals;
  SECTION("Open")
  {
    const auto test_file_path = test_dir.get_path() / "TEST_FILE"sv;

    REQUIRE(filesys.open(test_file_path.string()) == jewels::unexpected(make_error_code(ENOENT)));

    REQUIRE(
      create_test_file(test_file_path.string(), std::as_bytes(std::span{test_data.data(), test_data.size()}), filesys));

    REQUIRE(filesys.open(test_file_path.string()));

    if constexpr (std::is_same_v<TestType, FilesystemWrapper>)
    {
      filesys.inject_open_error(EBADMSG, 1U);
      REQUIRE(filesys.open(test_file_path.string()));
      REQUIRE(filesys.open(test_file_path.string()) == jewels::unexpected(make_error_code(EBADMSG)));
      REQUIRE(filesys.open(test_file_path.string()));
    }
  }

  SECTION("Read")
  {
    const auto test_file_path = test_dir.get_path() / "TEST_FILE"sv;

    REQUIRE(
      create_test_file(test_file_path.string(), std::as_bytes(std::span{test_data.data(), test_data.size()}), filesys));
    auto open_result = filesys.open(test_file_path.string());
    REQUIRE(open_result);

    std::string read_str(test_data.size(), '\0');
    REQUIRE(
      filesys.read(*open_result, std::as_writable_bytes(std::span{read_str.data(), read_str.size()})) ==
      read_str.size());
    REQUIRE(read_str == test_data);
    REQUIRE(filesys.read(*open_result, std::as_writable_bytes(std::span{read_str.data(), read_str.size()})) == 0U);
    REQUIRE(
      filesys.read(*open_result, 1U, std::as_writable_bytes(std::span{read_str.data(), read_str.size() - 2U})) ==
      read_str.size() - 2U);
    REQUIRE(read_str.substr(0, test_data.size() - 2U) == test_data.substr(1U, test_data.size() - 2U));
    REQUIRE(
      filesys.read(
        *open_result, test_data.size() - 1U, std::as_writable_bytes(std::span{read_str.data(), read_str.size()})) ==
      1U);

    REQUIRE(
      filesys.read(
        *open_result, test_data.size(), std::as_writable_bytes(std::span{read_str.data(), read_str.size()})) == 0U);

    if constexpr (std::is_same_v<TestType, FilesystemWrapper>)
    {
      filesys.inject_read_error(EBADMSG, 1U);
      REQUIRE(filesys.read(*open_result, std::as_writable_bytes(std::span{read_str.data(), read_str.size()})) == 0U);
      REQUIRE(
        filesys.read(*open_result, std::as_writable_bytes(std::span{read_str.data(), read_str.size()})) ==
        jewels::unexpected(make_error_code(EBADMSG)));
      REQUIRE(filesys.read(*open_result, std::as_writable_bytes(std::span{read_str.data(), read_str.size()})) == 0U);

      filesys.inject_read_error(EBADMSG, 1U);
      REQUIRE(
        filesys.read(*open_result, 0U, std::as_writable_bytes(std::span{read_str.data(), read_str.size()})) ==
        test_data.size());
      REQUIRE(
        filesys.read(*open_result, 0U, std::as_writable_bytes(std::span{read_str.data(), read_str.size()})) ==
        jewels::unexpected(make_error_code(EBADMSG)));
      REQUIRE(
        filesys.read(*open_result, 0U, std::as_writable_bytes(std::span{read_str.data(), read_str.size()})) ==
        test_data.size());
    }

    const auto open_fd = **open_result;
    REQUIRE(open_result->close());
    const FileDescriptor closed_fd{open_fd};
    REQUIRE(
      filesys.read(closed_fd, std::as_writable_bytes(std::span{read_str.data(), read_str.size()})) ==
      jewels::unexpected(make_error_code(EBADF)));
    REQUIRE(
      filesys.read(closed_fd, 0U, std::as_writable_bytes(std::span{read_str.data(), read_str.size()})) ==
      jewels::unexpected(make_error_code(EBADF)));

    if constexpr (std::is_same_v<TestType, FilesystemWrapper>)
    {
      open_result = filesys.open(test_file_path.string());
      REQUIRE(open_result);
      filesys.inject_read_io_errors(2U, 2U);
      REQUIRE(filesys.read(*open_result, 0U, std::as_writable_bytes(std::span{read_str.data(), 2U})) == 2U);
      REQUIRE(
        filesys.read(*open_result, 1U, std::as_writable_bytes(std::span{read_str.data(), 2U})) ==
        jewels::unexpected(make_error_code(EIO)));
      REQUIRE(filesys.read(*open_result, 4U, std::as_writable_bytes(std::span{read_str.data(), 2U})) == 2U);
      REQUIRE(
        filesys.read(*open_result, 3U, std::as_writable_bytes(std::span{read_str.data(), 2U})) ==
        jewels::unexpected(make_error_code(EIO)));
    }
  }

  SECTION("copy_file")
  {
    const auto get_permission_bits = [](const Path& path)
    {
      struct stat statbuf{};
      REQUIRE(::stat(path.c_str(), &statbuf) == 0);
      return static_cast<mode_t>(statbuf.st_mode & (static_cast<mode_t>(S_IRWXU) | S_IRWXG | S_IRWXO));
    };

    SECTION("copies data")
    {
      const auto test_file_path1 = test_dir.get_path() / "TEST_FILE1";
      const auto test_file_path2 = test_dir.get_path() / "TEST_FILE2";
      REQUIRE(create_test_file(
        test_file_path1.string(), std::as_bytes(std::span{test_data.data(), test_data.size()}), filesys));

      REQUIRE(filesys.copy_file(test_file_path1.string(), test_file_path2.string()));
      auto read_result = read_test_file(test_file_path2.string(), filesys);
      REQUIRE(read_result);
      REQUIRE(*read_result == test_data);
    }

    SECTION("copies entire file when block_size forces multiple sendfile calls")
    {
      const auto test_file_path1 = test_dir.get_path() / "TEST_FILE1_SMALL_BLOCK";
      const auto test_file_path2 = test_dir.get_path() / "TEST_FILE2_SMALL_BLOCK";
      constexpr size_t block_size = 7U;

      REQUIRE(create_test_file(
        test_file_path1.string(), std::as_bytes(std::span{test_data.data(), test_data.size()}), filesys));

      REQUIRE(filesys.copy_file(test_file_path1.string(), test_file_path2.string(), block_size));
      auto read_result = read_test_file(test_file_path2.string(), filesys);
      REQUIRE(read_result);
      REQUIRE(*read_result == test_data);
    }

    SECTION("preserves executable bits while keeping destination owner writable")
    {
      const auto source_path = test_dir.get_path() / "EXECUTABLE_SOURCE";
      const auto destination_path = test_dir.get_path() / "EXECUTABLE_COPY";

      constexpr mode_t executable_read_only_mode = S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
      constexpr mode_t expected_destination_mode = executable_read_only_mode | S_IWUSR;

      REQUIRE(
        create_test_file(source_path.string(), std::as_bytes(std::span{test_data.data(), test_data.size()}), filesys));
      REQUIRE(::chmod(source_path.c_str(), executable_read_only_mode) == 0);

      REQUIRE(filesys.copy_file(source_path.string(), destination_path.string()));

      auto read_result = read_test_file(destination_path.string(), filesys);
      REQUIRE(read_result);
      REQUIRE(*read_result == test_data);
      REQUIRE(get_permission_bits(destination_path) == expected_destination_mode);
    }

    SECTION("read-only source produces owner-writable destination")
    {
      const auto source_path = test_dir.get_path() / "READ_ONLY_SOURCE";
      const auto destination_path = test_dir.get_path() / "READ_ONLY_COPY";

      constexpr mode_t read_only_mode = S_IRUSR | S_IRGRP | S_IROTH;
      constexpr mode_t expected_destination_mode = read_only_mode | S_IWUSR;

      REQUIRE(
        create_test_file(source_path.string(), std::as_bytes(std::span{test_data.data(), test_data.size()}), filesys));
      REQUIRE(::chmod(source_path.c_str(), read_only_mode) == 0);

      REQUIRE(filesys.copy_file(source_path.string(), destination_path.string()));

      auto read_result = read_test_file(destination_path.string(), filesys);
      REQUIRE(read_result);
      REQUIRE(*read_result == test_data);
      REQUIRE(get_permission_bits(destination_path) == expected_destination_mode);

      auto open_result = filesys.open(destination_path.string(), O_RDWR);
      REQUIRE(open_result);
      const std::string overwrite_str = "ZZ";
      REQUIRE(
        filesys.write(*open_result, 0U, std::as_bytes(std::span{overwrite_str.data(), overwrite_str.size()})) ==
        overwrite_str.size());
    }

    SECTION("fails when destination directory is not writable")
    {
      if (::geteuid() == 0)
      {
        SUCCEED("Skipping permission denied check for root user");
      }
      else
      {
        const auto source_path = test_dir.get_path() / "SOURCE_FILE";
        const auto destination_dir = test_dir.get_path() / "NO_WRITE_DIR";
        const auto destination_path = destination_dir / "COPY_FILE";

        REQUIRE(create_test_file(
          source_path.string(), std::as_bytes(std::span{test_data.data(), test_data.size()}), filesys));
        REQUIRE(filesys.create_directory(destination_dir.string()));
        REQUIRE(::chmod(destination_dir.c_str(), S_IRUSR | S_IXUSR) == 0);

        REQUIRE(
          filesys.copy_file(source_path.string(), destination_path.string()) ==
          jewels::unexpected(make_error_code(EACCES)));

        REQUIRE(::chmod(destination_dir.c_str(), S_IRWXU) == 0);
      }
    }
  }

  SECTION("Write")
  {
    const auto test_file_path = test_dir.get_path() / "TEST_FILE";

    auto open_result = filesys.open(test_file_path.string(), O_CREAT | O_EXCL | O_WRONLY);
    REQUIRE(open_result);

    REQUIRE(
      filesys.write(*open_result, std::as_bytes(std::span{test_data.data(), test_data.size()})) == test_data.size());
    auto read_result = read_test_file(test_file_path.string(), filesys);
    REQUIRE(read_result);
    REQUIRE(*read_result == test_data);

    const std::string expected_str2 = "ABXXXXGHIJKLMNOPQRSTUVWXYZ";
    const std::string overwrite_str = "XXXX";
    REQUIRE(
      filesys.write(*open_result, 2U, std::as_bytes(std::span{overwrite_str.data(), overwrite_str.size()})) ==
      overwrite_str.size());
    read_result = read_test_file(test_file_path.string(), filesys);
    REQUIRE(read_result);
    REQUIRE(*read_result == expected_str2);

    if constexpr (std::is_same_v<TestType, FilesystemWrapper>)
    {
      filesys.inject_write_error(EBADMSG, 1U);
      REQUIRE(
        filesys.write(*open_result, std::as_bytes(std::span{test_data.data(), test_data.size()})) == test_data.size());
      REQUIRE(
        filesys.write(*open_result, std::as_bytes(std::span{test_data.data(), test_data.size()})) ==
        jewels::unexpected(make_error_code(EBADMSG)));
      REQUIRE(
        filesys.write(*open_result, std::as_bytes(std::span{test_data.data(), test_data.size()})) == test_data.size());

      filesys.inject_write_error(EBADMSG, 1U);
      REQUIRE(
        filesys.write(*open_result, 0U, std::as_bytes(std::span{test_data.data(), test_data.size()})) ==
        test_data.size());
      REQUIRE(
        filesys.write(*open_result, 0U, std::as_bytes(std::span{test_data.data(), test_data.size()})) ==
        jewels::unexpected(make_error_code(EBADMSG)));
      REQUIRE(
        filesys.write(*open_result, 0U, std::as_bytes(std::span{test_data.data(), test_data.size()})) ==
        test_data.size());
    }

    const auto open_fd = **open_result;
    REQUIRE(open_result->close());
    const FileDescriptor closed_fd{open_fd};
    REQUIRE(
      filesys.write(closed_fd, std::as_bytes(std::span{test_data.data(), test_data.size()})) ==
      jewels::unexpected(make_error_code(EBADF)));
    REQUIRE(
      filesys.write(closed_fd, 0U, std::as_bytes(std::span{test_data.data(), test_data.size()})) ==
      jewels::unexpected(make_error_code(EBADF)));
  }

  SECTION("SetPos/GetPos")
  {
    const auto test_file_path = test_dir.get_path() / "TEST_FILE";
    REQUIRE(
      create_test_file(test_file_path.string(), std::as_bytes(std::span{test_data.data(), test_data.size()}), filesys));

    auto open_result = filesys.open(test_file_path.string());
    REQUIRE(open_result);

    REQUIRE(filesys.get_position(*open_result) == 0U);
    REQUIRE(filesys.set_position(*open_result, 1U));
    REQUIRE(filesys.get_position(*open_result) == 1U);
    REQUIRE(filesys.set_position(*open_result, test_data.size()));
    REQUIRE(filesys.get_position(*open_result) == test_data.size());

    if constexpr (std::is_same_v<TestType, FilesystemWrapper>)
    {
      filesys.inject_lseek_error(EBADMSG, 1U);
      REQUIRE(filesys.get_position(*open_result) == test_data.size());
      REQUIRE(filesys.get_position(*open_result) == jewels::unexpected(make_error_code(EBADMSG)));
      REQUIRE(filesys.get_position(*open_result) == test_data.size());

      filesys.inject_lseek_error(EBADMSG, 1U);
      REQUIRE(filesys.set_position(*open_result, 0U));
      REQUIRE(filesys.set_position(*open_result, 0U) == jewels::unexpected(make_error_code(EBADMSG)));
      REQUIRE(filesys.set_position(*open_result, 0U));
    }

    const auto open_fd = **open_result;
    REQUIRE(open_result->close());
    const FileDescriptor closed_fd{open_fd};
    REQUIRE(filesys.get_position(closed_fd) == jewels::unexpected(make_error_code(EBADF)));
    REQUIRE(filesys.set_position(closed_fd, 0U) == jewels::unexpected(make_error_code(EBADF)));
  }

  SECTION("Symlink/Readlink/Unlink")
  {
    const auto test_file_path1 = test_dir.get_path() / "TEST_FILE1";
    const auto test_file_path2 = test_dir.get_path() / "TEST_FILE2";
    REQUIRE(filesys.unlink(test_file_path2.string()) == jewels::unexpected(make_error_code(ENOENT)));
    REQUIRE(create_test_file(test_file_path1.string(), {}, filesys));
    REQUIRE(filesys.create_symlink(test_file_path1.string_view(), test_file_path2.string_view()));
    REQUIRE(filesys.read_symlink(test_file_path2) == test_file_path1.string_view());
    REQUIRE(filesys.unlink(test_file_path2.string()));

    if constexpr (std::is_same_v<TestType, FilesystemWrapper>)
    {
      filesys.inject_link_error(EBADMSG, 1U);
      REQUIRE(filesys.create_symlink(test_file_path1.string(), test_file_path2.string()));
      REQUIRE(
        filesys.create_symlink(test_file_path1.string(), test_file_path2.string()) ==
        jewels::unexpected(make_error_code(EBADMSG)));
      REQUIRE(
        filesys.create_symlink(test_file_path1.string(), test_file_path2.string()) ==
        jewels::unexpected(make_error_code(EEXIST)));

      filesys.inject_readlink_error(EBADMSG, 1U);
      REQUIRE(filesys.read_symlink(test_file_path2) == test_file_path1.string_view());
      REQUIRE(filesys.read_symlink(test_file_path2) == jewels::unexpected(make_error_code(EBADMSG)));
      REQUIRE(filesys.read_symlink(test_file_path2) == test_file_path1.string_view());

      filesys.inject_unlink_error(EBADMSG, 1U);
      REQUIRE(filesys.unlink(test_file_path2.string()));
      REQUIRE(filesys.unlink(test_file_path2.string()) == jewels::unexpected(make_error_code(EBADMSG)));
      REQUIRE(filesys.unlink(test_file_path2.string()) == jewels::unexpected(make_error_code(ENOENT)));
    }
  }

  SECTION("Hardlink/Unlink")
  {
    const auto test_file_path1 = test_dir.get_path() / "TEST_FILE1";
    const auto test_file_path2 = test_dir.get_path() / "TEST_FILE2";
    REQUIRE(filesys.unlink(test_file_path2.string()) == jewels::unexpected(make_error_code(ENOENT)));
    REQUIRE(create_test_file(test_file_path1.string(), {}, filesys));
    REQUIRE(filesys.create_hardlink(test_file_path1.string_view(), test_file_path2.string_view()));
    REQUIRE(filesys.exists(test_file_path2.string_view()) == true);
    REQUIRE(filesys.unlink(test_file_path2.string()));

    if constexpr (std::is_same_v<TestType, FilesystemWrapper>)
    {
      filesys.inject_link_error(EBADMSG, 1U);
      REQUIRE(filesys.create_hardlink(test_file_path1.string(), test_file_path2.string()));
      REQUIRE(
        filesys.create_hardlink(test_file_path1.string(), test_file_path2.string()) ==
        jewels::unexpected(make_error_code(EBADMSG)));
      REQUIRE(
        filesys.create_hardlink(test_file_path1.string(), test_file_path2.string()) ==
        jewels::unexpected(make_error_code(EEXIST)));

      filesys.inject_unlink_error(EBADMSG, 1U);
      REQUIRE(filesys.unlink(test_file_path2.string()));
      REQUIRE(filesys.unlink(test_file_path2.string()) == jewels::unexpected(make_error_code(EBADMSG)));
      REQUIRE(filesys.unlink(test_file_path2.string()) == jewels::unexpected(make_error_code(ENOENT)));
    }
  }

  SECTION("touch")
  {
    const auto test_file_path1 = test_dir.get_path() / "TEST_DIR2//TEST_FILE1";
    REQUIRE(filesys.exists(test_file_path1.string()) == false);
    REQUIRE(filesys.exists(test_file_path1.parent_path().string()) == false);
    REQUIRE(filesys.touch(test_file_path1.string()));
    REQUIRE(filesys.exists(test_file_path1.string()) == true);
    const auto before_last_write_time = filesys.get_last_write_time(test_file_path1.string());
    // Otherwise we'll complete too quickly and the time won't change
    // This tests the O_TRUNC path
    std::this_thread::sleep_for(std::chrono::seconds(1));
    REQUIRE(filesys.touch(test_file_path1.string()));
    const auto after_last_write_time = filesys.get_last_write_time(test_file_path1.string());
    REQUIRE(*before_last_write_time < *after_last_write_time);

    if constexpr (std::is_same_v<TestType, Filesystem>)
    {
      const auto test_file_path2 = test_dir.get_path() / "TEST_DIR2//TEST_FILE2";
      REQUIRE(filesys.touch(test_file_path2.string(), S_IRWXU));
      struct stat statbuf{};
      REQUIRE(::stat(test_file_path2.c_str(), &statbuf) == 0);
      const auto expected_mode2 = S_IFREG | S_IRWXU;
      REQUIRE(statbuf.st_mode == expected_mode2);
    }
  }

  SECTION("create_temporary_directory")
  {
    // NOLINTNEXTLINE(concurrency-mt-unsafe) this test is single-threaded
    auto* old_value = std::getenv("XDG_SESSION_DIR");
    // NOLINTNEXTLINE(concurrency-mt-unsafe) this test is single-threaded
    ::setenv("XDG_SESSION_DIR", "/tmp/very/long/path/that/does/not/exist", 1);
    const auto possible_temporary_directory = filesys.create_temporary_directory();
    REQUIRE(possible_temporary_directory);
    REQUIRE(filesys.exists(possible_temporary_directory->string()) == true);
    REQUIRE(filesys.is_directory(possible_temporary_directory->string()) == true);
    if (old_value != nullptr)
    {
      // NOLINTNEXTLINE(concurrency-mt-unsafe) this test is single-threaded
      ::setenv("XDG_SESSION_DIR", old_value, 1);
    }
  }

  SECTION("create_temporary_file")
  {
    // NOLINTNEXTLINE(concurrency-mt-unsafe) this test is single-threaded
    auto* old_value = std::getenv("XDG_SESSION_DIR");
    // NOLINTNEXTLINE(concurrency-mt-unsafe) this test is single-threaded
    ::setenv("XDG_SESSION_DIR", "/tmp/very/long/path/that/does/not/exist/", 1);
    const auto possible_temporary_file = filesys.create_temporary_file();
    REQUIRE(possible_temporary_file);
    REQUIRE(filesys.exists(possible_temporary_file->first.string()) == true);
    REQUIRE(filesys.is_regular_file(possible_temporary_file->first.string()) == true);
    if (old_value != nullptr)
    {
      // NOLINTNEXTLINE(concurrency-mt-unsafe) this test is single-threaded
      ::setenv("XDG_SESSION_DIR", old_value, 1);
    }
  }

  SECTION("create_temporary_file with absolute parent path")
  {
    auto optional_path =
      std::make_optional<filesystem::Path>(filesystem::Path{"/tmp/foo/really/here", memory_resource});
    const auto possible_temporary_file = filesys.create_temporary_file(optional_path);
    REQUIRE(possible_temporary_file);
    REQUIRE(filesys.exists(possible_temporary_file->first.string()) == true);
    REQUIRE(filesys.is_regular_file(possible_temporary_file->first.string()) == true);
  }

  SECTION("create_temporary_file with relative parent path")
  {
    auto optional_path = std::make_optional<filesystem::Path>(filesystem::Path{"foo/really/here", memory_resource});
    const auto possible_temporary_file = filesys.create_temporary_file(optional_path);
    REQUIRE(possible_temporary_file);
    REQUIRE(filesys.exists(possible_temporary_file->first.string()) == true);
    REQUIRE(filesys.is_regular_file(possible_temporary_file->first.string()) == true);
  }

  SECTION("Mkdir/Exists/IsFile/IsDir/GetSize/LastWriteTime")
  {
    const auto test_file_path1 = test_dir.get_path() / "TEST_FILE1";
    const auto test_dir_path1 = test_dir.get_path() / "TEST_DIR1";
    const auto test_dir_path2 = test_dir.get_path() / "TEST_DIR2//TEST_SUBDIR//";
    const std::string expected_str = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

    REQUIRE(filesys.exists(test_file_path1.string()) == false);
    REQUIRE(filesys.exists(test_dir_path1.string()) == false);
    REQUIRE(filesys.is_regular_file(test_file_path1.string()) == false);
    REQUIRE(filesys.is_regular_file(test_dir_path1.string()) == false);
    REQUIRE(filesys.is_directory(test_file_path1.string()) == false);
    REQUIRE(filesys.is_directory(test_dir_path1.string()) == false);
    REQUIRE(filesys.get_size(test_file_path1.string()) == jewels::unexpected(make_error_code(ENOENT)));
    REQUIRE(filesys.create_directory("/___BAD_PATH___/___BAD_PATH___") == jewels::unexpected(make_error_code(ENOENT)));
    REQUIRE(filesys.create_directory(test_dir_path1.string()));
    REQUIRE(filesys.create_directories(test_dir_path2.string()));
    REQUIRE(create_test_file(
      test_file_path1.string(), std::as_bytes(std::span{expected_str.data(), expected_str.size()}), filesys));

    REQUIRE(filesys.exists(test_file_path1.string()) == true);
    REQUIRE(filesys.exists(test_dir_path1.string()) == true);
    REQUIRE(filesys.exists(test_dir_path2.string()) == true);
    REQUIRE(filesys.is_regular_file(test_file_path1.string()) == true);
    REQUIRE(filesys.is_regular_file(test_dir_path1.string()) == false);
    REQUIRE(filesys.is_regular_file(test_dir_path2.string()) == false);
    REQUIRE(filesys.is_directory(test_file_path1.string()) == false);
    REQUIRE(filesys.is_directory(test_dir_path1.string()) == true);
    REQUIRE(filesys.is_directory(test_dir_path2.string()) == true);
    REQUIRE(filesys.get_size(test_file_path1.string()) == expected_str.size());

    auto write_time_result = filesys.get_last_write_time(test_file_path1.string());
    REQUIRE(write_time_result);
    auto time_to_set = write_time_result.value() + std::chrono::seconds(1) + std::chrono::milliseconds(1);
    REQUIRE(filesys.set_last_write_time(test_file_path1.string(), time_to_set));
    write_time_result = filesys.get_last_write_time(test_file_path1.string());
    REQUIRE(write_time_result);
    REQUIRE(write_time_result.value() == time_to_set);
    const auto open_result = filesys.open(test_file_path1.string(), O_RDWR);
    REQUIRE(open_result);
    write_time_result = filesys.get_last_write_time(open_result.value());
    REQUIRE(write_time_result.value() == time_to_set);
    time_to_set = write_time_result.value() + std::chrono::seconds(1) + std::chrono::milliseconds(1);
    REQUIRE(filesys.set_last_write_time(open_result.value(), time_to_set));
    write_time_result = filesys.get_last_write_time(open_result.value());
    REQUIRE(write_time_result);
    REQUIRE(write_time_result.value() == time_to_set);

    if constexpr (std::is_same_v<TestType, FilesystemWrapper>)
    {
      filesys.inject_mkdir_error(EBADMSG, 1U);
      REQUIRE(filesys.create_directory(test_dir_path1.string()) == jewels::unexpected(make_error_code(EEXIST)));
      REQUIRE(filesys.create_directory(test_dir_path1.string()) == jewels::unexpected(make_error_code(EBADMSG)));
      REQUIRE(filesys.create_directory(test_dir_path1.string()) == jewels::unexpected(make_error_code(EEXIST)));

      filesys.inject_mkdir_error(EBADMSG, 1U);
      REQUIRE(filesys.create_directories(test_dir_path2.string()));
      REQUIRE(filesys.create_directories(test_dir_path2.string()) == jewels::unexpected(make_error_code(EBADMSG)));
      REQUIRE(filesys.create_directories(test_dir_path2.string()));

      filesys.inject_stat_error(EBADMSG, 1U);
      REQUIRE(filesys.exists(test_file_path1.string()) == true);
      REQUIRE(filesys.exists(test_file_path1.string()) == jewels::unexpected(make_error_code(EBADMSG)));
      REQUIRE(filesys.exists(test_file_path1.string()) == true);

      filesys.inject_stat_error(EBADMSG, 1U);
      REQUIRE(filesys.is_regular_file(test_file_path1.string()) == true);
      REQUIRE(filesys.is_regular_file(test_file_path1.string()) == jewels::unexpected(make_error_code(EBADMSG)));
      REQUIRE(filesys.is_regular_file(test_file_path1.string()) == true);

      filesys.inject_stat_error(EBADMSG, 1U);
      REQUIRE(filesys.is_directory(test_file_path1.string()) == false);
      REQUIRE(filesys.is_directory(test_file_path1.string()) == jewels::unexpected(make_error_code(EBADMSG)));
      REQUIRE(filesys.is_directory(test_file_path1.string()) == false);

      filesys.inject_stat_error(EBADMSG, 1U);
      REQUIRE(filesys.get_size(test_file_path1.string()) == expected_str.size());
      REQUIRE(filesys.get_size(test_file_path1.string()) == jewels::unexpected(make_error_code(EBADMSG)));
      REQUIRE(filesys.get_size(test_file_path1.string()) == expected_str.size());

      filesys.inject_stat_error(EBADMSG, 1U);
      REQUIRE(filesys.get_last_write_time(test_file_path1.string()));
      REQUIRE(filesys.get_last_write_time(test_file_path1.string()) == jewels::unexpected(make_error_code(EBADMSG)));
      REQUIRE(filesys.get_last_write_time(test_file_path1.string()));

      filesys.inject_stat_error(EBADMSG, 1U);
      REQUIRE(filesys.get_last_write_time(open_result.value()));
      REQUIRE(filesys.get_last_write_time(open_result.value()) == jewels::unexpected(make_error_code(EBADMSG)));
      REQUIRE(filesys.get_last_write_time(open_result.value()));

      filesys.inject_stat_error(EBADMSG, 1U);
      REQUIRE(filesys.set_last_write_time(test_file_path1.string(), jewels::time::SyncClock::now()));
      REQUIRE(
        filesys.set_last_write_time(test_file_path1.string(), jewels::time::SyncClock::now()) ==
        jewels::unexpected(make_error_code(EBADMSG)));
      REQUIRE(filesys.set_last_write_time(test_file_path1.string(), jewels::time::SyncClock::now()));

      filesys.inject_stat_error(EBADMSG, 1U);
      REQUIRE(filesys.set_last_write_time(open_result.value(), jewels::time::SyncClock::now()));
      REQUIRE(
        filesys.set_last_write_time(open_result.value(), jewels::time::SyncClock::now()) ==
        jewels::unexpected(make_error_code(EBADMSG)));
      REQUIRE(filesys.set_last_write_time(open_result.value(), jewels::time::SyncClock::now()));
    }
  }

  SECTION("Readdir")
  {
    constexpr auto* file_name1 = "TEST_FILE1";
    constexpr auto* file_name2 = "TEST_FILE2";
    constexpr auto* dir_name1 = "TEST_DIR1";
    constexpr auto* dir_name2 = "TEST_DIR2";

    const auto test_file_path1 = test_dir.get_path() / file_name1;
    const auto test_file_path2 = test_dir.get_path() / file_name2;
    const auto test_dir_path1 = test_dir.get_path() / dir_name1;
    const auto test_dir_path2 = test_dir.get_path() / dir_name2;

    REQUIRE(filesys.read_directory("__BAD_PATH__") == jewels::unexpected(make_error_code(ENOENT)));
    REQUIRE(
      filesys.read_directory("__BAD_PATH__", [](const auto&) { return true; }) ==
      jewels::unexpected(make_error_code(ENOENT)));
    const auto filter_fn = [](const auto&) { return true; };
    REQUIRE(filesys.read_directory("__BAD_PATH__", filter_fn) == jewels::unexpected(make_error_code(ENOENT)));

    REQUIRE(create_test_file(test_file_path1.string(), {}, filesys));
    REQUIRE(create_test_file(test_file_path2.string(), {}, filesys));
    REQUIRE(filesys.create_directory(test_dir_path1.string()));
    REQUIRE(filesys.create_directory(test_dir_path2.string()));

    const auto readdir_result = filesys.read_directory(test_dir.get_path().string());
    REQUIRE(
      readdir_result == std::pmr::vector<filesystem::Path>{
                          {dir_name1, memory_resource},
                          {dir_name2, memory_resource},
                          {file_name1, memory_resource},
                          {file_name2, memory_resource}});

    const auto readdir_filtered_result =
      filesys.read_directory(test_dir.get_path().string(), [](const auto& dent) { return dent.d_type == DT_REG; });
    REQUIRE(
      readdir_filtered_result ==
      std::pmr::vector<filesystem::Path>{{file_name1, memory_resource}, {file_name2, memory_resource}});

    if constexpr (std::is_same_v<TestType, FilesystemWrapper>)
    {
      filesys.inject_readdir_error(EBADMSG, 1U);
      REQUIRE(filesys.read_directory(test_dir.get_path().string()));
      REQUIRE(filesys.read_directory(test_dir.get_path().string()) == jewels::unexpected(make_error_code(EBADMSG)));
      REQUIRE(filesys.read_directory(test_dir.get_path().string()));

      filesys.inject_readdir_error(EBADMSG, 1U);
      REQUIRE(filesys.read_directory(test_dir.get_path().string(), [](const auto&) { return true; }));
      REQUIRE(
        filesys.read_directory(test_dir.get_path().string(), [](const auto&) { return true; }) ==
        jewels::unexpected(make_error_code(EBADMSG)));
      REQUIRE(filesys.read_directory(test_dir.get_path().string(), [](const auto&) { return true; }));

      filesys.inject_readdir_error(EBADMSG, 1U);
      REQUIRE(filesys.read_directory(test_dir.get_path().string(), filter_fn));
      REQUIRE(
        filesys.read_directory(test_dir.get_path().string(), filter_fn) ==
        jewels::unexpected(make_error_code(EBADMSG)));
      REQUIRE(filesys.read_directory(test_dir.get_path().string(), filter_fn));
    }
  }

  SECTION("Readdir Recursive")
  {
    constexpr auto* dir_name = "DIR_NAME";
    constexpr auto* subdir_name1 = "SUB_DIR1";
    constexpr auto* subfile_name1 = "TEST_FILE1";
    constexpr auto* subdir_name2 = "TEST_DIR2";
    constexpr auto* subfile_name2 = "TEST_FILE2";

    const auto dir_path = test_dir.get_path() / dir_name;
    const auto subdir_path1 = dir_path / subdir_name1;
    const auto subfile_path1 = subdir_path1 / subfile_name1;
    const auto subdir_path2 = dir_path / subdir_name2;
    const auto subfile_path2 = subdir_path2 / subfile_name2;

    REQUIRE(filesys.read_directories("__BAD_PATH__") == jewels::unexpected(make_error_code(ENOENT)));
    REQUIRE(
      filesys.read_directories("__BAD_PATH__", [](const auto&) { return true; }) ==
      jewels::unexpected(make_error_code(ENOENT)));
    const auto filter_fn = [](const auto&) { return true; };
    REQUIRE(filesys.read_directories("__BAD_PATH__", filter_fn) == jewels::unexpected(make_error_code(ENOENT)));

    REQUIRE(filesys.create_directory(dir_path.string()));
    REQUIRE(filesys.create_directory(subdir_path1.string()));
    REQUIRE(filesys.create_directory(subdir_path2.string()));
    REQUIRE(create_test_file(subfile_path1.string(), {}, filesys));
    REQUIRE(create_test_file(subfile_path2.string(), {}, filesys));

    const auto readdir_result = filesys.read_directories(test_dir.get_path().string());
    REQUIRE(readdir_result);
    REQUIRE(
      *readdir_result == std::pmr::vector<filesystem::Path>{
                           filesystem::Path{dir_name, memory_resource},
                           filesystem::Path{dir_name, memory_resource} / subdir_name1,
                           filesystem::Path{dir_name, memory_resource} / subdir_name1 / subfile_name1,
                           filesystem::Path{dir_name, memory_resource} / subdir_name2,
                           filesystem::Path{dir_name, memory_resource} / subdir_name2 / subfile_name2});

    const auto readdir_filtered_result =
      filesys.read_directories(test_dir.get_path().string(), [](const auto& dent) { return dent.d_type == DT_REG; });
    REQUIRE(
      readdir_filtered_result == std::pmr::vector<filesystem::Path>{
                                   filesystem::Path{dir_name, memory_resource} / subdir_name1 / subfile_name1,
                                   filesystem::Path{dir_name, memory_resource} / subdir_name2 / subfile_name2});

    if constexpr (std::is_same_v<TestType, FilesystemWrapper>)
    {
      filesys.inject_readdir_error(EBADMSG, 1U);
      REQUIRE(filesys.read_directories(test_dir.get_path().string()));
      REQUIRE(filesys.read_directories(test_dir.get_path().string()) == jewels::unexpected(make_error_code(EBADMSG)));
      REQUIRE(filesys.read_directories(test_dir.get_path().string()));

      filesys.inject_readdir_error(EBADMSG, 1U);
      REQUIRE(filesys.read_directories(test_dir.get_path().string(), [](const auto&) { return true; }));
      REQUIRE(
        filesys.read_directories(test_dir.get_path().string(), [](const auto&) { return true; }) ==
        jewels::unexpected(make_error_code(EBADMSG)));
      REQUIRE(filesys.read_directories(test_dir.get_path().string(), [](const auto&) { return true; }));

      filesys.inject_readdir_error(EBADMSG, 1U);
      REQUIRE(filesys.read_directories(test_dir.get_path().string(), filter_fn));
      REQUIRE(
        filesys.read_directories(test_dir.get_path().string(), filter_fn) ==
        jewels::unexpected(make_error_code(EBADMSG)));
      REQUIRE(filesys.read_directories(test_dir.get_path().string(), filter_fn));
    }
  }

  SECTION("Get space information")
  {
    auto space_result = filesys.get_space_information("__BAD_PATH__");
    REQUIRE(space_result == jewels::unexpected(make_error_code(ENOENT)));

    space_result = filesys.get_space_information(test_dir.get_path().string());
    REQUIRE(space_result);
    REQUIRE(space_result->capacity >= space_result->free);
    REQUIRE(space_result->free >= space_result->available);
  }

  SECTION("remove")
  {
    SECTION("file removal")
    {
      SECTION("path does not exist")
      {
        const auto remove_result = filesys.remove("__BAD_PATH__");
        REQUIRE(remove_result == jewels::unexpected(make_error_code(ENOENT)));
      }

      SECTION("success")
      {
        const auto test_file_path = test_dir.get_path() / "TEST_FILE";

        REQUIRE(create_test_file(test_file_path.string(), {}, filesys));

        const auto remove_result = filesys.remove(test_file_path.string());
        REQUIRE(remove_result);

        const auto exists_result = filesys.exists(test_file_path.string());
        REQUIRE(exists_result);
        REQUIRE_FALSE(exists_result.value());
      }
    }

    SECTION("directory removal")
    {
      const auto test_dir_path = test_dir.get_path() / "TEST_DIR";
      REQUIRE(filesys.create_directory(test_dir_path.string()));

      const auto test_file_path = test_dir_path / "TEST_FILE";
      REQUIRE(create_test_file(test_file_path.string(), {}, filesys));

      auto remove_result = filesys.remove(test_dir_path.string());
      REQUIRE(remove_result == jewels::unexpected(make_error_code(ENOTEMPTY)));

      REQUIRE(filesys.exists(test_dir_path.string()));

      REQUIRE(filesys.remove(test_file_path.string()));
      REQUIRE(filesys.remove(test_dir_path.string()));

      REQUIRE(filesys.exists(test_file_path.string()) == false);
      REQUIRE(filesys.exists(test_dir_path.string()) == false);
    }
  }

  SECTION("remove all")
  {
    SECTION("file removal")
    {
      SECTION("path does not exist")
      {
        const auto remove_result = filesys.remove_all("__BAD_PATH__");
        REQUIRE(remove_result == jewels::unexpected(make_error_code(ENOENT)));
      }

      SECTION("success")
      {
        const auto test_file_path = test_dir.get_path() / "TEST_FILE";
        REQUIRE(create_test_file(test_file_path.string(), {}, filesys));

        const auto remove_result = filesys.remove_all(test_file_path.string());
        REQUIRE(remove_result == 1UL);

        const auto exists_result = filesys.exists(test_file_path.string());
        REQUIRE(exists_result);
        REQUIRE_FALSE(exists_result.value());
      }
    }

    SECTION("directory removal")
    {
      // empty directory
      const auto test_dir_path = test_dir.get_path() / "TEST_DIR";
      REQUIRE(filesys.create_directory(test_dir_path.string()));

      auto remove_result = filesys.remove_all(test_dir_path.string());
      REQUIRE(remove_result);
      REQUIRE(remove_result.value() == 1UL);

      // Directory structure:
      // TEST_DIR/
      // -> TEST_FILE
      // -> TEST_DIR_CHILD/
      // --> TEST_FILE_CHILD

      REQUIRE(filesys.create_directory(test_dir_path.string()));

      const auto test_file_path = test_dir_path / "TEST_FILE";
      REQUIRE(create_test_file(test_file_path.string(), {}, filesys));

      const auto test_dir_child_path = test_dir_path / "TEST_DIR_CHILD";
      REQUIRE(filesys.create_directory(test_dir_child_path.string()));

      const auto exists_result = filesys.exists(test_dir_child_path.string());
      REQUIRE(exists_result);
      REQUIRE(exists_result.value());

      const auto test_file_child_path = test_dir_child_path / "TEST_FILE_CHILD";
      REQUIRE(create_test_file(test_file_child_path.string(), {}, filesys));

      remove_result = filesys.remove_all(test_dir_path.string());
      REQUIRE(remove_result);
      REQUIRE(remove_result.value() == 4UL);

      REQUIRE(filesys.exists(test_file_path.string()) == false);
      REQUIRE(filesys.exists(test_dir_path.string()) == false);
      REQUIRE(filesys.exists(test_file_child_path.string()) == false);
      REQUIRE(filesys.exists(test_dir_child_path.string()) == false);
    }
  }

  SECTION("Search Path")
  {
    using namespace std::literals;

    {
      const auto result = filesys.search_path("nonexistent-binary-xyz");
      REQUIRE(!result);
      REQUIRE(result.error() == make_error_code(ENOENT));
    }

    const auto bin_dir1 = test_dir.get_path() / "bin1"sv;
    const auto bin_dir2 = test_dir.get_path() / "bin2"sv;
    REQUIRE(filesys.create_directory(bin_dir1));
    REQUIRE(filesys.create_directory(bin_dir2));

    const auto path_value =
      (std::pmr::string{bin_dir1.c_str(), memory_resource} + ":" + std::pmr::string{bin_dir2.c_str(), memory_resource});
    REQUIRE(::setenv("PATH", path_value.c_str(), 1) == 0); // NOLINT(concurrency-mt-unsafe) test is single threaded

    constexpr auto testapp = "testapp"sv;
    const auto app_path1 = bin_dir1 / testapp;
    REQUIRE(create_test_file(app_path1.string(), {}, filesys));

    REQUIRE(::chmod(app_path1.c_str(), S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) == 0);

    // First directory can contain an app
    const auto result1 = filesys.search_path(testapp);
    REQUIRE(result1);
    REQUIRE(result1.value().string() == app_path1.string());

    // Second directory can contain an app
    constexpr auto testapp2 = "testapp2"sv;
    const auto app_path2 = bin_dir2 / testapp2;
    REQUIRE(create_test_file(app_path2.string(), {}, filesys));
    REQUIRE(::chmod(app_path2.c_str(), S_IRWXU) == 0);

    const auto result2 = filesys.search_path(testapp2);
    REQUIRE(result2);
    REQUIRE(result2.value().string() == app_path2.string());

    // Test that non-executable files are not returned
    constexpr auto notexec = "notexec"sv;
    const auto non_exec_path = bin_dir1 / notexec;
    REQUIRE(create_test_file(non_exec_path.string(), {}, filesys));
    // Don't set executable bit - permissions default to 0644

    const auto result3 = filesys.search_path(notexec);
    REQUIRE(!result3);
    REQUIRE(result3.error() == make_error_code(ENOENT));

    {
      REQUIRE(::chmod(app_path1.c_str(), S_IRUSR | S_IWUSR | S_IRUSR | S_IXUSR) == 0); // 500 octal
      REQUIRE(filesys.search_path(testapp));
      REQUIRE(::chmod(app_path1.c_str(), S_IRUSR | S_IWUSR | S_IXGRP) == 0); // 050 octal
      REQUIRE(filesys.search_path(testapp));
      REQUIRE(::chmod(app_path1.c_str(), S_IRUSR | S_IWUSR | S_IXOTH) == 0); // 005 octal
      REQUIRE(filesys.search_path(testapp));
    }

    // Test error responses
    if constexpr (std::is_same_v<TestType, FilesystemWrapper>)
    {
      // Found it previously, but now we can't access it
      filesys.inject_search_path_error(EACCES, 0U);
      const auto error_result = filesys.search_path(testapp);
      REQUIRE(!error_result);
      REQUIRE(error_result.error() == make_error_code(EACCES));

      const auto success_result = filesys.search_path(testapp);
      REQUIRE(success_result);
    }

    if constexpr (std::is_same_v<TestType, FilesystemWrapper>)
    {
      const auto injected_path = test_dir.get_path() / "injected" / "path"sv;
      const auto injected_path_obj = filesystem::Path{injected_path.string(), memory_resource};

      filesys.inject_search_path_result(injected_path_obj);

      const auto result4 = filesys.search_path("any-binary");
      REQUIRE(result4);
      REQUIRE(result4.value().string() == injected_path.string());

      // Second call should NOT return injected result (was cleared after first use)
      const auto result5 = filesys.search_path("any-binary");
      REQUIRE(!result5);
      REQUIRE(result5.error() == make_error_code(ENOENT));
    }

    // Clean up environment
    ::unsetenv("PATH"); // NOLINT(concurrency-mt-unsafe) thread is single threaded
  }
}

} // namespace
} // namespace jewels::filesystem::testing
