// IWYU pragma: private, include "clockwork/logging/nolint_helper.hh"
#pragma once

#include "clockwork/logging/nolint_helper.hh"

#include "clockwork/logging/log_error.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/std/expected.hh"

#include <cstddef>
#include <cstdlib>
#include <span>
#include <string_view>
#include <tuple>

namespace clockwork_logging::nolint_helper
{

std::string_view byte_span_to_string_view(std::span<const std::byte> data)
{
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) Cast to char* for string view
  return std::string_view{reinterpret_cast<const char*>(data.data()), data.size()};
}

template <typename T>
std::span<const T> byte_span_to_value_span(std::span<const std::byte> data)
{
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) Cast to T* for value span
  return std::span<const T>{reinterpret_cast<const T*>(data.data()), data.size() / sizeof(T)};
}

template <typename T>
std::span<T> byte_span_to_value_span(std::span<std::byte> data)
{
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) Cast to T* for value span
  return std::span<T>{reinterpret_cast<T*>(data.data()), data.size() / sizeof(T)};
}

template <typename T>
LogExpected<T*> byte_span_to_mutable_value_ptr(std::span<std::byte> data)
{
  if (data.size() != sizeof(T))
  {
    jewels::log_cerr_error(
      "Invalid span size {} in byte_span_to_mutable_value_ptr, expected {}", data.size(), sizeof(T));
    return jewels::unexpected(LogError::invalid_argument);
  }
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) Convert to T* for value pointer
  return reinterpret_cast<T*>(data.data());
}

template <typename T>
LogExpected<const T*> byte_span_to_value_ptr(std::span<const std::byte> data)
{
  if (data.size() != sizeof(T))
  {
    jewels::log_cerr_error("Invalid span size {} in byte_span_to_value_ptr, expected {}", data.size(), sizeof(T));
    return jewels::unexpected(LogError::invalid_argument);
  }
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) Convert to T* for value pointer
  return reinterpret_cast<const T*>(data.data());
}

char* char_ptr_to_span_element(std::span<std::byte> data, size_t offset)
{
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) Convert from byte* to char*
  return reinterpret_cast<char*>(&std::span{data.data(), offset + 1U}.back());
}

char* increment_char_ptr(char* ptr, size_t count)
{
  return &std::span{ptr, count + 1U}.back();
}

const char* get_environment_variable(const char* env_var)
{
  // NOLINTNEXTLINE(concurrency-mt-unsafe) Environment variables are not modified by the process
  return std::getenv(env_var);
}

void set_environment_variable(const char* env_var, const char* env_val)
{
  // NOLINTNEXTLINE(concurrency-mt-unsafe) Caller is expected to know that nothing is accessing the environment
  std::ignore = ::setenv(env_var, env_val, 0);
}

} // namespace clockwork_logging::nolint_helper
