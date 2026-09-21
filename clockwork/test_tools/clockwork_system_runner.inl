// IWYU pragma: private, include "clockwork/test_tools/clockwork_system_runner.hh"
#pragma once
#include "clockwork/test_tools/clockwork_system_runner.hh"

#include "clockwork/test_tools/synthetic_message_fetcher.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/time/sync_time.hh"

#include <memory>
#include <memory_resource>
#include <string_view>
#include <vector>

namespace clockwork
{
template <typename MessageType>
void MessageInjectorSystemRunner::add_message(
  const MessageType& message, jewels::time::SyncTime publish_time, std::string_view channel_name)
{
  message_fetcher_->add_message(message, publish_time, channel_name);
}

template <typename MessageType>
jewels::BinaryOutcome MessageInjectorSystemRunner::get_messages_from_channel(
  std::string_view channel_name,
  jewels::Out<std::pmr::vector<jewels::memory::pmr_unique_ptr<MessageType>>> messages) const
{
  return message_writer_->get_messages_from_channel(channel_name, start_time_, end_time_, jewels::Out{*messages});
}

template <typename MessageType>
jewels::BinaryOutcome MessageInjectorSystemRunner::get_messages_and_metadata_from_channel(
  std::string_view channel_name,
  jewels::Out<std::pmr::vector<testing::MessageWithMetadata<MessageType>>> messages) const
{
  return message_writer_->get_messages_and_metadata_from_channel(
    channel_name, start_time_, end_time_, jewels::Out{*messages});
}
} // namespace clockwork
