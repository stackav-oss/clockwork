// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/test_tools/synthetic_message_fetcher.hh"

#include "jewels/callsig/outcome.hh"
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
  if (current_it_ == messages_.end())
  {
    return std::nullopt;
  }
  const auto& retval = *current_it_;
  ++current_it_;
  return retval;
}

jewels::expected<void, jewels::MonoError> SyntheticMessageFetcher::initialize()
{
  current_it_ = messages_.begin();
  return {};
}

jewels::BinaryOutcome SyntheticMessageFetcher::reset() noexcept
{
  current_it_ = messages_.begin();
  return jewels::success;
}

} // namespace clockwork::testing
