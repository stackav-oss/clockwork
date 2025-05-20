// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/testing/filesystem_wrapper.hh"

#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/std/expected.hh"

#include <cerrno>
#include <memory_resource>
#include <unistd.h>
#include <utility>

namespace jewels::filesystem::testing
{

FilesystemWrapper::FilesystemWrapper(jewels::memory::MemoryResource memory_resource)
  : memory_resource_(std::move(memory_resource)), wrapped_(memory_resource_)
{
}

void FilesystemWrapper::set_verbosity(Filesystem::ErrorVerbosity verbosity) noexcept
{
  wrapped_.set_verbosity(verbosity);
}

[[nodiscard]] jewels::expected<FileDescriptor, ErrorCode>
FilesystemWrapper::open(std::string_view file_path, int32_t mode_flags, uint32_t perms)
{
  if (const auto inject_result = check_error_injection_state(maybe_inject_open_error_state_); !inject_result)
  {
    jewels::log_cerr_error("Injecting error in open_read: {}", inject_result.error().message());
    return jewels::unexpected(inject_result.error());
  }
  return wrapped_.open(file_path, mode_flags, perms);
}

[[nodiscard]] jewels::expected<size_t, ErrorCode>
FilesystemWrapper::read(const FileDescriptor& file_desc, std::span<std::byte> data)
{
  if (const auto inject_result = check_error_injection_state(maybe_inject_read_error_state_); !inject_result)
  {
    jewels::log_cerr_error("Injecting error in read: {}", inject_result.error().message());
    return jewels::unexpected(inject_result.error());
  }
  if (maybe_read_io_error_range_ && !data.empty())
  {
    const auto signed_offset = lseek(*file_desc, 0, SEEK_CUR);
    if (signed_offset < 0)
    {
      const auto error = make_error_code(errno);
      jewels::log_cerr_error("Failed to lseek(SEEK_CUR) on file descriptor {}': {}", *file_desc, error.message());
      return jewels::unexpected(error);
    }
    const auto offset = static_cast<size_t>(signed_offset);
    if (
      ((offset + data.size() - 1U) >= maybe_read_io_error_range_->start_offset) &&
      (offset <= maybe_read_io_error_range_->end_offset))
    {
      const auto error = make_error_code(EIO);
      jewels::log_cerr_error("Injecting error in read: {}", error.message());
      return jewels::unexpected(error);
    }
  }
  return wrapped_.read(file_desc, data);
}

[[nodiscard]] jewels::expected<size_t, ErrorCode>
FilesystemWrapper::read(const FileDescriptor& file_desc, const size_t offset, std::span<std::byte> data)
{
  if (const auto inject_result = check_error_injection_state(maybe_inject_read_error_state_); !inject_result)
  {
    jewels::log_cerr_error("Injecting error in read: {}", inject_result.error().message());
    return jewels::unexpected(inject_result.error());
  }
  if (maybe_read_io_error_range_ && !data.empty())
  {
    if (
      ((offset + data.size() - 1U) >= maybe_read_io_error_range_->start_offset) &&
      (offset <= maybe_read_io_error_range_->end_offset))
    {
      const auto error = make_error_code(EIO);
      jewels::log_cerr_error("Injecting error in read: {}", error.message());
      return jewels::unexpected(error);
    }
  }
  return wrapped_.read(file_desc, offset, data);
}

[[nodiscard]] jewels::expected<size_t, ErrorCode>
FilesystemWrapper::write(const FileDescriptor& file_desc, std::span<const std::byte> data)
{
  if (const auto inject_result = check_error_injection_state(maybe_inject_write_error_state_); !inject_result)
  {
    jewels::log_cerr_error("Injecting error in write: {}", inject_result.error().message());
    return jewels::unexpected(inject_result.error());
  }
  return wrapped_.write(file_desc, data);
}

[[nodiscard]] jewels::expected<size_t, ErrorCode>
FilesystemWrapper::write(const FileDescriptor& file_desc, const size_t offset, std::span<const std::byte> data)
{
  if (const auto inject_result = check_error_injection_state(maybe_inject_write_error_state_); !inject_result)
  {
    jewels::log_cerr_error("Injecting error in write: {}", inject_result.error().message());
    return jewels::unexpected(inject_result.error());
  }
  return wrapped_.write(file_desc, offset, data);
}

[[nodiscard]] jewels::expected<void, ErrorCode>
FilesystemWrapper::set_position(const FileDescriptor& file_desc, size_t offset)
{
  if (const auto inject_result = check_error_injection_state(maybe_inject_lseek_error_state_); !inject_result)
  {
    jewels::log_cerr_error("Injecting error in set_position: {}", inject_result.error().message());
    return inject_result;
  }
  return wrapped_.set_position(file_desc, offset);
}

[[nodiscard]] jewels::expected<size_t, ErrorCode> FilesystemWrapper::get_position(const FileDescriptor& file_desc)
{
  if (const auto inject_result = check_error_injection_state(maybe_inject_lseek_error_state_); !inject_result)
  {
    jewels::log_cerr_error("Injecting error in get_position: {}", inject_result.error().message());
    return jewels::unexpected(inject_result.error());
  }
  return wrapped_.get_position(file_desc);
}

[[nodiscard]] jewels::expected<size_t, ErrorCode> FilesystemWrapper::get_size(std::string_view file_path)
{
  if (const auto inject_result = check_error_injection_state(maybe_inject_stat_error_state_); !inject_result)
  {
    jewels::log_cerr_error("Injecting error in get_size: {}", inject_result.error().message());
    return jewels::unexpected(inject_result.error());
  }
  return wrapped_.get_size(file_path);
}

[[nodiscard]] jewels::expected<size_t, ErrorCode> FilesystemWrapper::get_size(const FileDescriptor& file_desc)
{
  if (const auto inject_result = check_error_injection_state(maybe_inject_stat_error_state_); !inject_result)
  {
    jewels::log_cerr_error("Injecting error in get_size: {}", inject_result.error().message());
    return jewels::unexpected(inject_result.error());
  }
  return wrapped_.get_size(file_desc);
}

[[nodiscard]] jewels::expected<jewels::time::SyncTime, ErrorCode>
FilesystemWrapper::get_last_write_time(const FileDescriptor& file_desc)
{
  if (const auto inject_result = check_error_injection_state(maybe_inject_stat_error_state_); !inject_result)
  {
    jewels::log_cerr_error("Injecting error in get_last_write_time: {}", inject_result.error().message());
    return jewels::unexpected(inject_result.error());
  }
  return wrapped_.get_last_write_time(file_desc);
}

[[nodiscard]] jewels::expected<jewels::time::SyncTime, ErrorCode>
FilesystemWrapper::get_last_write_time(std::string_view file_path)
{
  if (const auto inject_result = check_error_injection_state(maybe_inject_stat_error_state_); !inject_result)
  {
    jewels::log_cerr_error("Injecting error in get_last_write_time: {}", inject_result.error().message());
    return jewels::unexpected(inject_result.error());
  }
  return wrapped_.get_last_write_time(file_path);
}

[[nodiscard]] jewels::expected<void, ErrorCode>
FilesystemWrapper::set_last_write_time(const FileDescriptor& file_desc, jewels::time::SyncTime time_to_set)
{
  if (const auto inject_result = check_error_injection_state(maybe_inject_stat_error_state_); !inject_result)
  {
    jewels::log_cerr_error("Injecting error in set_last_write_time: {}", inject_result.error().message());
    return jewels::unexpected(inject_result.error());
  }
  return wrapped_.set_last_write_time(file_desc, time_to_set);
}

[[nodiscard]] jewels::expected<void, ErrorCode>
FilesystemWrapper::set_last_write_time(std::string_view file_path, jewels::time::SyncTime time_to_set)
{
  if (const auto inject_result = check_error_injection_state(maybe_inject_stat_error_state_); !inject_result)
  {
    jewels::log_cerr_error("Injecting error in set_last_write_time: {}", inject_result.error().message());
    return jewels::unexpected(inject_result.error());
  }
  return wrapped_.set_last_write_time(file_path, time_to_set);
}

[[nodiscard]] jewels::expected<void, ErrorCode>
FilesystemWrapper::rename(std::string_view old_path, std::string_view new_path)
{
  if (const auto inject_result = check_error_injection_state(maybe_inject_rename_error_state_); !inject_result)
  {
    jewels::log_cerr_error("Injecting error in rename: {}", inject_result.error().message());
    return inject_result;
  }
  return wrapped_.rename(old_path, new_path);
}

[[nodiscard]] jewels::expected<void, ErrorCode> FilesystemWrapper::unlink(std::string_view path)
{
  if (const auto inject_result = check_error_injection_state(maybe_inject_unlink_error_state_); !inject_result)
  {
    jewels::log_cerr_error("Injecting error in unlink: {}", inject_result.error().message());
    return inject_result;
  }
  return wrapped_.unlink(path);
}

[[nodiscard]] jewels::expected<void, ErrorCode>
FilesystemWrapper::create_directory(std::string_view path, uint32_t perms)
{
  if (const auto inject_result = check_error_injection_state(maybe_inject_mkdir_error_state_); !inject_result)
  {
    jewels::log_cerr_error("Injecting error in create_directory: {}", inject_result.error().message());
    return inject_result;
  }
  return wrapped_.create_directory(path, perms);
}

[[nodiscard]] jewels::expected<void, ErrorCode>
FilesystemWrapper::create_directories(std::string_view path, uint32_t perms)
{
  if (const auto inject_result = check_error_injection_state(maybe_inject_mkdir_error_state_); !inject_result)
  {
    jewels::log_cerr_error("Injecting error in create_directories: {}", inject_result.error().message());
    return inject_result;
  }
  return wrapped_.create_directories(path, perms);
}

[[nodiscard]] jewels::expected<void, ErrorCode>
FilesystemWrapper::create_symlink(std::string_view target_path, std::string_view link_path)
{
  if (const auto inject_result = check_error_injection_state(maybe_inject_symlink_error_state_); !inject_result)
  {
    jewels::log_cerr_error("Injecting error in create_symlink: {}", inject_result.error().message());
    return inject_result;
  }
  return wrapped_.create_symlink(target_path, link_path);
}

[[nodiscard]] jewels::expected<std::pmr::string, ErrorCode> FilesystemWrapper::read_symlink(std::string_view link_path)
{
  if (const auto inject_result = check_error_injection_state(maybe_inject_readlink_error_state_); !inject_result)
  {
    jewels::log_cerr_error("Injecting error in read_symlink {}", inject_result.error().message());
    return jewels::unexpected(inject_result.error());
  }
  return wrapped_.read_symlink(link_path);
}

[[nodiscard]] jewels::expected<std::pmr::vector<filesystem::Path>, ErrorCode>
FilesystemWrapper::read_directory(std::string_view path)
{
  if (const auto inject_result = check_error_injection_state(maybe_inject_readdir_error_state_); !inject_result)
  {
    jewels::log_cerr_error("Injecting error in read_directory: {}", inject_result.error().message());
    return jewels::unexpected(inject_result.error());
  }
  return wrapped_.read_directory(path);
}

[[nodiscard]] jewels::expected<std::pmr::vector<filesystem::Path>, ErrorCode>
FilesystemWrapper::read_directories(std::string_view path, bool ignore_permission_denied)
{
  if (const auto inject_result = check_error_injection_state(maybe_inject_readdir_error_state_); !inject_result)
  {
    jewels::log_cerr_error("Injecting error in read_directory: {}", inject_result.error().message());
    return jewels::unexpected(inject_result.error());
  }
  return wrapped_.read_directories(path, ignore_permission_denied);
}

[[nodiscard]] jewels::expected<bool, ErrorCode> FilesystemWrapper::exists(std::string_view path)
{
  if (const auto inject_result = check_error_injection_state(maybe_inject_stat_error_state_); !inject_result)
  {
    jewels::log_cerr_error("Injecting error in exists: {}", inject_result.error().message());
    return jewels::unexpected(inject_result.error());
  }
  return wrapped_.exists(path);
}

[[nodiscard]] jewels::expected<bool, ErrorCode> FilesystemWrapper::is_directory(std::string_view path)
{
  if (const auto inject_result = check_error_injection_state(maybe_inject_stat_error_state_); !inject_result)
  {
    jewels::log_cerr_error("Injecting error in is_directory: {}", inject_result.error().message());
    return jewels::unexpected(inject_result.error());
  }
  return wrapped_.is_directory(path);
}

[[nodiscard]] jewels::expected<bool, ErrorCode> FilesystemWrapper::is_regular_file(std::string_view path)
{
  if (const auto inject_result = check_error_injection_state(maybe_inject_stat_error_state_); !inject_result)
  {
    jewels::log_cerr_error("Injecting error in is_regular_file: {}", inject_result.error().message());
    return jewels::unexpected(inject_result.error());
  }
  return wrapped_.is_regular_file(path);
}

[[nodiscard]] jewels::expected<Filesystem::SpaceInformation, ErrorCode>
FilesystemWrapper::get_space_information(std::string_view path)
{
  return wrapped_.get_space_information(path);
}

[[nodiscard]] jewels::expected<void, ErrorCode> FilesystemWrapper::remove(std::string_view path)
{
  if (const auto inject_result = check_error_injection_state(maybe_inject_remove_error_state_); !inject_result)
  {
    jewels::log_cerr_error("Injecting error in remove: {}", inject_result.error().message());
    return jewels::unexpected(inject_result.error());
  }
  return wrapped_.remove(path);
}

[[nodiscard]] jewels::expected<size_t, ErrorCode> FilesystemWrapper::remove_all(std::string_view path)
{
  if (const auto inject_result = check_error_injection_state(maybe_inject_remove_error_state_); !inject_result)
  {
    jewels::log_cerr_error("Injecting error in remove_all: {}", inject_result.error().message());
    return jewels::unexpected(inject_result.error());
  }
  return wrapped_.remove_all(path);
}

[[nodiscard]] jewels::expected<void, ErrorCode> FilesystemWrapper::touch(std::string_view file_path)
{
  if (const auto inject_result = check_error_injection_state(maybe_inject_touch_error_state_); !inject_result)
  {
    jewels::log_cerr_error("Injecting error in touch: {}", inject_result.error().message());
    return jewels::unexpected(inject_result.error());
  }
  return wrapped_.touch(file_path);
}

[[nodiscard]] jewels::expected<filesystem::Path, ErrorCode> FilesystemWrapper::create_temporary_directory()
{
  if (const auto inject_result = check_error_injection_state(maybe_inject_create_temporary_directory_error_state_);
      !inject_result)
  {
    jewels::log_cerr_error("Injecting error in create_temporary_directory: {}", inject_result.error().message());
    return jewels::unexpected(inject_result.error());
  }
  return wrapped_.create_temporary_directory();
}

[[nodiscard]] jewels::expected<void, ErrorCode>
FilesystemWrapper::copy_file(std::string_view old_path, std::string_view new_path)
{
  if (const auto inject_result = check_error_injection_state(maybe_inject_stat_error_state_); !inject_result)
  {
    jewels::log_cerr_error("Injecting error in copy_file: {}", inject_result.error().message());
    return jewels::unexpected(inject_result.error());
  }
  return wrapped_.copy_file(old_path, new_path);
}

void FilesystemWrapper::inject_touch_error(int32_t error_code, size_t skip_count)
{
  maybe_inject_touch_error_state_ = InjectedErrorState{.error = make_error_code(error_code), .skip_count = skip_count};
}

void FilesystemWrapper::inject_copy_file_error(int32_t error_code, size_t skip_count)
{
  maybe_inject_copy_file_error_state_ =
    InjectedErrorState{.error = make_error_code(error_code), .skip_count = skip_count};
}

void FilesystemWrapper::inject_create_temporary_directory_error(int32_t error_code, size_t skip_count)
{
  maybe_inject_create_temporary_directory_error_state_ =
    InjectedErrorState{.error = make_error_code(error_code), .skip_count = skip_count};
}

void FilesystemWrapper::inject_open_error(int32_t error_code, size_t skip_count)
{
  maybe_inject_open_error_state_ = InjectedErrorState{.error = make_error_code(error_code), .skip_count = skip_count};
}

void FilesystemWrapper::inject_read_error(int32_t error_code, size_t skip_count)
{
  maybe_inject_read_error_state_ = InjectedErrorState{.error = make_error_code(error_code), .skip_count = skip_count};
}

void FilesystemWrapper::inject_write_error(int32_t error_code, size_t skip_count)
{
  maybe_inject_write_error_state_ = InjectedErrorState{.error = make_error_code(error_code), .skip_count = skip_count};
}

void FilesystemWrapper::inject_lseek_error(int32_t error_code, size_t skip_count)
{
  maybe_inject_lseek_error_state_ = InjectedErrorState{.error = make_error_code(error_code), .skip_count = skip_count};
}

void FilesystemWrapper::inject_read_io_errors(size_t offset, size_t length)
{
  if (length != 0U)
  {
    maybe_read_io_error_range_ = InjectedErrorRange{.start_offset = offset, .end_offset = offset + length - 1U};
  }
}

void FilesystemWrapper::inject_stat_error(int32_t error_code, size_t skip_count)
{
  maybe_inject_stat_error_state_ = InjectedErrorState{.error = make_error_code(error_code), .skip_count = skip_count};
}

void FilesystemWrapper::inject_rename_error(int32_t error_code, size_t skip_count)
{
  maybe_inject_rename_error_state_ = InjectedErrorState{.error = make_error_code(error_code), .skip_count = skip_count};
}

void FilesystemWrapper::inject_unlink_error(int32_t error_code, size_t skip_count)
{
  maybe_inject_unlink_error_state_ = InjectedErrorState{.error = make_error_code(error_code), .skip_count = skip_count};
}

void FilesystemWrapper::inject_mkdir_error(int32_t error_code, size_t skip_count)
{
  maybe_inject_mkdir_error_state_ = InjectedErrorState{.error = make_error_code(error_code), .skip_count = skip_count};
}

void FilesystemWrapper::inject_symlink_error(int32_t error_code, size_t skip_count)
{
  maybe_inject_symlink_error_state_ =
    InjectedErrorState{.error = make_error_code(error_code), .skip_count = skip_count};
}

void FilesystemWrapper::inject_readlink_error(int32_t error_code, size_t skip_count)
{
  maybe_inject_readlink_error_state_ =
    InjectedErrorState{.error = make_error_code(error_code), .skip_count = skip_count};
}

void FilesystemWrapper::inject_readdir_error(int32_t error_code, size_t skip_count)
{
  maybe_inject_readdir_error_state_ =
    InjectedErrorState{.error = make_error_code(error_code), .skip_count = skip_count};
}

void FilesystemWrapper::inject_remove_error(int32_t error_code, size_t skip_count)
{
  maybe_inject_remove_error_state_ = InjectedErrorState{.error = make_error_code(error_code), .skip_count = skip_count};
}

[[nodiscard]] jewels::expected<void, ErrorCode> FilesystemWrapper::check_error_injection_state(
  std::optional<FilesystemWrapper::InjectedErrorState>& maybe_injected_error_state)
{
  if (maybe_injected_error_state)
  {
    if (maybe_injected_error_state->skip_count == 0U)
    {
      const auto error = maybe_injected_error_state->error;
      maybe_injected_error_state = std::nullopt;
      return jewels::unexpected(error);
    }
    --maybe_injected_error_state->skip_count;
  }
  return {};
}

} // namespace jewels::filesystem::testing
