// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/offboard/init_aws_api.hh"

#include "jewels/log_cerr/log_cerr.hh"

#include <aws/core/Aws.h>

#include <csignal>

namespace clockwork_logging::offboard
{

void init_aws_api()
{
  [[maybe_unused]] static const auto aws_initialized = []()
  {
    const Aws::SDKOptions options;
    Aws::InitAPI(options);
    if (const auto signal_rc = std::signal(SIGPIPE, SIG_IGN); signal_rc == SIG_ERR)
    {
      jewels::log_cerr_warn("Failed to ignore SIGPIPE");
    }
    return true;
  }();
}

} // namespace clockwork_logging::offboard
