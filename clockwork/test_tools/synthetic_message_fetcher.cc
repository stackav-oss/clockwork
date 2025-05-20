// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/test_tools/synthetic_message_fetcher.hh"

#include "jewels/std/expected.hh"

namespace clockwork::testing
{
MessageWriterContainer::MessageWriterContainer(jewels::memory::MemoryResource memres)
  : memory_resource_(memres), received_messages_(memres)
{
}

void MessageWriterContainer::message_received_callback(MessageInfoView message_info)
{
  received_messages_.emplace_back(MultiMessageInfoData::from_view(message_info, memory_resource_));
}

std::optional<MultiMessageInfoData> MessageWriterContainer::try_pop_message()
{
  if (received_messages_.empty())
  {
    return std::nullopt;
  }
  auto retval = received_messages_.front();
  received_messages_.pop_front();

  return retval;
}

jewels::expected<void, jewels::MonoError> MessageWriterContainer::initialize()
{
  return {};
}

SyntheticMessageFetcher::SyntheticMessageFetcher(jewels::memory::MemoryResource memres)
  : memory_resource_(std::move(memres))
{
}

std::optional<::clockwork::MultiMessageInfoData> SyntheticMessageFetcher::try_fetch_message()
{
  if (messages_.empty())
  {
    return std::nullopt;
  }
  auto retval = *messages_.begin();
  messages_.erase(messages_.begin());
  return retval;
}

jewels::expected<void, jewels::MonoError> SyntheticMessageFetcher::initialize()
{
  return {};
}
} // namespace clockwork::testing
