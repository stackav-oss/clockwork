// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/channel_type.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/channel_observer_client.hh"
#include "clockwork/pinion/observer.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"

#include <memory_resource>
#include <string>
#include <string_view>

namespace clockwork::pinion
{

/// Observer to receive notifications of new messages on a shared memory channel
class ChannelObserver final : public clockwork::pinion::Observer
{
public:
  /// Constructor
  /// @param[in] memory_resource Memory resource
  /// @param[in] buffer_ptr Pinion buffer pointer
  /// @param[in] client_ptr Observer client pointer
  /// @param[in] channel_name Channel name
  /// @param[in] channel_type Channel type
  ChannelObserver(
    jewels::memory::MemoryResource memory_resource,
    jewels::memory::ObjectPtr<clockwork::pinion::Buffer> buffer_ptr,
    jewels::memory::ObjectPtr<ChannelObserverClient> client_ptr,
    std::string_view channel_name,
    clockwork_logging::ChannelType channel_type);

  ~ChannelObserver() final = default;

  ChannelObserver(const ChannelObserver&) noexcept = default;
  ChannelObserver(ChannelObserver&&) noexcept = default;
  ChannelObserver& operator=(const ChannelObserver&) noexcept = default;
  ChannelObserver& operator=(ChannelObserver&&) noexcept = default;

  /// @see Observer::notify
  void notify(const clockwork::pinion::Observer::Event& event) final;

  /// Channel name accessor
  /// @return channel name
  [[nodiscard]] std::string_view get_channel_name() const noexcept;

private:
  /// Pinion buffer pointer
  jewels::memory::ObjectPtr<clockwork::pinion::Buffer> buffer_ptr_;

  /// Client pointer
  jewels::memory::ObjectPtr<ChannelObserverClient> client_ptr_;

  /// Channel name
  std::pmr::string channel_name_;

  /// Channel type
  clockwork_logging::ChannelType channel_type_;

  /// Next expected buffer iterator
  clockwork::pinion::BufferIterator next_iterator_{};
};

} // namespace clockwork::pinion
