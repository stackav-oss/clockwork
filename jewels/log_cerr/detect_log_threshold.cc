// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/log_cerr/detect_log_threshold.hh"

#include <wise_enum.h>

#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace jewels
{

DetectLogThreshold::DetectLogThreshold() noexcept
{
  // NOLINTNEXTLINE(concurrency-mt-unsafe)  Thread safety is noted in the api docs.
  const char* env_var = std::getenv("STACK_LOG_CERR_THRESHOLD");
  if (env_var == nullptr)
  {
    return;
  }

  for (auto level_option : wise_enum::range<LogLevel>)
  {
    if (strncmp(env_var, level_option.name.data(), level_option.name.size()) == 0)
    {
      log_threshold_ = level_option.value;
      return;
    }
  }

  std::cerr << "Unable to determine log threshold " << env_var << ", using default INFO\n";
}

} // namespace jewels
