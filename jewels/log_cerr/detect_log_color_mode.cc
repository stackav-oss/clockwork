// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/log_cerr/detect_log_color_mode.hh"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <unistd.h> // isatty

namespace jewels
{

DetectLogColorMode::DetectLogColorMode() noexcept
{
  // NOLINTNEXTLINE(concurrency-mt-unsafe)  Thread safety is noted in the api docs.
  const char* env_var = std::getenv("CLOCKWORK_LOG_CERR_COLOR_MODE");
  if (env_var == nullptr)
  {
    return;
  }

  if (strcmp(env_var, "always") == 0)
  {
    use_color_ = true;
    return;
  }

  if (strcmp(env_var, "never") == 0)
  {
    return;
  }

  if (strcmp(env_var, "auto") == 0)
  {
    constexpr int cerr_fileno = 2;
    use_color_ = isatty(cerr_fileno) != 0;
    return;
  }

  std::cerr << "Unable to determine color mode, LOG_CERR_COLOR_MODE is " << env_var
            << R"(, expected "always"|"never"|"auto")" << '\n';
}

} // namespace jewels
