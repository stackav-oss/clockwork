// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/offboard/init_aws_api.hh"

#include "clockwork/logging/nolint_helper.hh"
#include "jewels/log_cerr/log_cerr.hh"

#include <aws/core/Aws.h>
#include <aws/core/utils/logging/ConsoleLogSystem.h>
#include <aws/core/utils/logging/LogLevel.h>

#include <csignal>
#include <functional>
#include <memory>

namespace clockwork_logging::offboard
{

namespace
{

/// Environment variable used to enable logging for the S3 interface
constexpr auto enable_s3_logging_env_var = "ENABLE_S3_LOGGING";

} // namespace

[[nodiscard]] bool s3_logging_is_enabled()
{
  static auto logging_flag = []()
  {
    const auto* const logging_flag_ptr = nolint_helper::get_environment_variable(enable_s3_logging_env_var);
    return logging_flag_ptr != nullptr;
  }();
  return logging_flag;
}

namespace
{

struct AwsManager
{
  AwsManager()
  {

    if (s3_logging_is_enabled())
    {
      options.loggingOptions.logLevel = Aws::Utils::Logging::LogLevel::Debug;
      options.loggingOptions.logger_create_fn = []
      { return std::make_shared<Aws::Utils::Logging::ConsoleLogSystem>(Aws::Utils::Logging::LogLevel::Debug); };
    }
    Aws::InitAPI(options);
    if (const auto signal_rc = std::signal(SIGPIPE, SIG_IGN); signal_rc == SIG_ERR)
    {
      jewels::log_cerr_warn("Failed to ignore SIGPIPE");
    }
  }

  AwsManager(const AwsManager&) = delete;
  AwsManager(AwsManager&&) = delete;
  AwsManager& operator=(const AwsManager&) = delete;
  AwsManager& operator=(AwsManager&&) = delete;

  ~AwsManager()
  {
    Aws::ShutdownAPI(options);
  }

  Aws::SDKOptions options;
};

} // namespace

void init_aws_api()
{
  [[maybe_unused]] static const AwsManager aws_manager;
}

} // namespace clockwork_logging::offboard
