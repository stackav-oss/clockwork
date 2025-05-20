// IWYU pragma: private, include "clockwork/logging/log_error.hh"
#pragma once

#include "clockwork/logging/log_error.hh"

#include "jewels/filesystem/error_code.hh"
#include "jewels/std/expected.hh"

#include <wise_enum.h>

#include <cerrno>
#include <iosfwd>
#include <ostream>
#include <string_view>

namespace clockwork_logging
{

[[nodiscard]] LogError to_log_error(jewels::filesystem::ErrorCode error_code)
{
  switch (error_code.value())
  {
  case ENOENT:
    return LogError::no_such_file_or_directory;
  case EFAULT:
    return LogError::bad_address;
  case EBADF:
    return LogError::bad_file_descriptor;
  case EBUSY:
    return LogError::device_or_resource_busy;
  case ENOTEMPTY:
    return LogError::directory_not_empty;
  case EEXIST:
    return LogError::file_exists;
  case EFBIG:
    return LogError::file_too_large;
  case ENAMETOOLONG:
    return LogError::filename_too_long;
  case EINTR:
    return LogError::interrupted;
  case EINVAL:
    return LogError::invalid_argument;
  case EIO:
    return LogError::io_error;
  case EISDIR:
    return LogError::is_a_directory;
  case ENOTDIR:
    return LogError::not_a_directory;
  case EACCES:
    return LogError::permission_denied;
  case EROFS:
    return LogError::read_only_file_system;
  case ENOSPC:
    return LogError::no_space_on_device;
  default:
    return LogError::unspecified_system_error;
  }
}

std::ostream& operator<<(std::ostream& ostream, LogError error)
{
  ostream << wise_enum::to_string(error);
  return ostream;
}

template <typename T>
std::ostream& operator<<(std::ostream& ostream, LogExpected<T> result)
{
  if (result)
  {
    ostream << "{?}";
  }
  else
  {
    ostream << wise_enum::to_string(result.error());
  }
  return ostream;
}

std::ostream& operator<<(std::ostream& ostream, jewels::unexpected<LogError> error)
{
  ostream << wise_enum::to_string(error.value());
  return ostream;
}

} // namespace clockwork_logging
