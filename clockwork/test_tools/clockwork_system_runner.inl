// IWYU pragma: private, include "clockwork/test_tools/clockwork_system_runner.hh"
#pragma once
#include "clockwork/test_tools/clockwork_system_runner.hh"

#include "jewels/time/sync_time.hh"

#include <memory>
#include <string_view>

namespace clockwork
{
template <typename MessageType>
void MessageInjectorSystemRunner::add_message(
  const MessageType& message, jewels::time::SyncTime publish_time, std::string_view channel_name)
{
  message_fetcher_->add_message(message, publish_time, channel_name);
}
} // namespace clockwork
