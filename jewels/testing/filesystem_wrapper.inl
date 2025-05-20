#pragma once

// IWYU pragma: private, include "jewels/testing/filesystem_wrapper.hh"

#include "jewels/testing/filesystem_wrapper.hh"

#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/std/expected.hh"

#include <memory_resource>
#include <string_view>
#include <utility>
#include <vector>

namespace jewels::filesystem::testing
{

template <typename FilterFunctionType>
[[nodiscard]] jewels::expected<std::pmr::vector<filesystem::Path>, ErrorCode>
FilesystemWrapper::read_directory(std::string_view path, FilterFunctionType&& filter_fn)
{
  if (const auto inject_result = check_error_injection_state(maybe_inject_readdir_error_state_); !inject_result)
  {
    jewels::log_cerr_error("Injecting error in read_directory: {}", inject_result.error().message());
    return jewels::unexpected(inject_result.error());
  }
  return wrapped_.read_directory(path, std::forward<FilterFunctionType>(filter_fn));
}

template <typename FilterFunctionType>
[[nodiscard]] jewels::expected<std::pmr::vector<filesystem::Path>, ErrorCode>
FilesystemWrapper::read_directory(std::string_view path, const FilterFunctionType& filter_fn)
{
  if (const auto inject_result = check_error_injection_state(maybe_inject_readdir_error_state_); !inject_result)
  {
    jewels::log_cerr_error("Injecting error in read_directory: {}", inject_result.error().message());
    return jewels::unexpected(inject_result.error());
  }
  return wrapped_.read_directory(path, filter_fn);
}

template <typename FilterFunctionType>
[[nodiscard]] jewels::expected<std::pmr::vector<filesystem::Path>, ErrorCode> FilesystemWrapper::read_directories(
  std::string_view path, FilterFunctionType&& filter_fn, bool ignore_permission_denied)
{
  if (const auto inject_result = check_error_injection_state(maybe_inject_readdir_error_state_); !inject_result)
  {
    jewels::log_cerr_error("Injecting error in read_directory: {}", inject_result.error().message());
    return jewels::unexpected(inject_result.error());
  }
  return wrapped_.read_directories(path, std::forward<FilterFunctionType>(filter_fn), ignore_permission_denied);
}

template <typename FilterFunctionType>
[[nodiscard]] jewels::expected<std::pmr::vector<filesystem::Path>, ErrorCode> FilesystemWrapper::read_directories(
  std::string_view path, const FilterFunctionType& filter_fn, bool ignore_permission_denied)
{
  if (const auto inject_result = check_error_injection_state(maybe_inject_readdir_error_state_); !inject_result)
  {
    jewels::log_cerr_error("Injecting error in read_directory: {}", inject_result.error().message());
    return jewels::unexpected(inject_result.error());
  }
  return wrapped_.read_directories(path, filter_fn, ignore_permission_denied);
}

} // namespace jewels::filesystem::testing
