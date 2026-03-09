// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/repr_iface.hh"
#include "clockwork/tools/channel_spy/channel_spy_config_clk_cc.hh"
#include "clockwork/tools/channel_spy/channel_spy_subscriber.hh"
#include "clockwork/tools/channel_spy/types.hh"
#include "jewels/memory/memory_resource.hh"

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace clockwork::tools
{

/// Shared memory channel metadata
struct SpyChannelMetadata
{
  /// Channel name
  std::string channel_name{};

  /// Schema name
  std::string schema_name{};

  /// Schema definition
  std::vector<std::byte> schema_definition{};
};

/// Class to read from pinion channels by spying on their shared memory buffers
class ChannelSpy
{
public:
  /// Default channel spy configuration file path
  static constexpr auto default_channel_spy_config_path_format = "{}/clockwork/channel_spy_config.tachyon";

  /// Format string for the channel spy configuration file path when the socket namespace is specified
  static constexpr auto namespace_channel_spy_config_path_format = "{}/clockwork/{}/channel_spy_config.tachyon";

  /// Default subscriber polling interval
  static constexpr auto default_polling_interval = std::chrono::milliseconds(1);

  /// Constructor
  /// @param[in] shm_root_dir Pinion shared memory root directory
  /// @param[in] tmp_dir Temporary file directory
  /// @param[in] socket_ns Pinion socket namespace
  explicit ChannelSpy(
    std::string_view shm_root_dir = "/dev/shm", std::string_view tmp_dir = "/tmp", std::string_view socket_ns = "");

  ~ChannelSpy() noexcept = default;
  ChannelSpy(const ChannelSpy&) = delete;
  ChannelSpy& operator=(const ChannelSpy&) = delete;
  ChannelSpy(ChannelSpy&&) = delete;
  ChannelSpy& operator=(ChannelSpy&&) = delete;

  /// @return The channels available for reading
  /// @throws runtime_error on failure
  [[nodiscard]] std::vector<SpyChannelMetadata> channels() const;

  /// Read the channel spy configuration file, maybe_config_ will have a value on success
  /// @return The channel spy configuration for the local machine
  /// @throws runtime_error on failure
  [[nodiscard]] std::unique_ptr<Tappy<ChannelSpyConfig<>>> read_channel_spy_config() const;

  /// Add channel spy subscriber to receive raw message data
  /// @param[in] channel_name Channel name
  /// @param[in] callback_fn Raw message data callback function
  /// @throws runtime_error on failure
  void subscribe(std::string_view channel_name, const RawMessageCallback& callback_fn);

  /// Add channel spy subscriber to python callbacks
  /// @param[in] channel_name Channel name
  /// @param[in] callback_fn Python callback function
  /// @throws runtime_error on failure
  void subscribe(std::string_view channel_name, const PythonCallback& callback_fn);

  /// Add a channel spy subscriber to receive deserialized messages
  /// @tparam MessageType Subscribed message type
  /// @param[in] channel_name Channel name
  /// @param[in] callback_fn Message callback function
  /// @throws runtime_error on failure
  template <typename MessageType>
  void subscribe(std::string_view channel_name, const DeserializedMessageCallback<MessageType>& callback_fn)
    requires(TappyType<MessageType> || TachyonType<MessageType>);

  /// Periodically poll for new messages and issue callbacks for new messages
  /// @param[in] polling_interval Polling interval
  void run(std::chrono::nanoseconds polling_interval = default_polling_interval);

  /// Poll each subscribed channel once and issue callbacks for new messages
  void run_once();

private:
  /// Memory resource
  jewels::memory::MemoryResource memory_resource_;

  /// Pinion shared memory root directory
  std::string shm_root_dir_;

  /// Temporary file directory
  std::string tmp_dir_;

  /// Pinion socket namespace
  std::string socket_ns_;

  /// Channel subscribers
  std::vector<std::unique_ptr<ChannelSpySubscriber>> subscribers_;
};

} // namespace clockwork::tools

#include "clockwork/tools/channel_spy/channel_spy.inl"
