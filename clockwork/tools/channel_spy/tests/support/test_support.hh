// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/repr_iface.hh"
#include "clockwork/tools/channel_spy/channel_spy_config_clk_cc.hh"

#include <memory>
#include <string_view>

namespace clockwork::tools::tests::support
{

/// Test channel name
static constexpr auto test_channel_name1 = "channel_1";

/// Test channel name
static constexpr auto test_channel_name2 = "channel_2";

/// Generate valid test channel spy configuration for testing subscriptions
/// @tparam<MessageType> Test message type
/// @return Generated configuration
template <typename MessageType>
  requires TappyType<MessageType>
[[nodiscard]] std::unique_ptr<Tappy<ChannelSpyConfig<>>> gen_channel_spy_config();

/// Write the spy configuration file to the test shared memory directory
/// @param[in] tmp_dir Temporary directory
/// @param[in] config Channel spy configuration
void write_channel_spy_config_file(
  std::string_view tmp_dir, std::string_view socket_ns, const Tappy<ChannelSpyConfig<>>& config);

} // namespace clockwork::tools::tests::support

#include "clockwork/tools/channel_spy/tests/support/test_support.inl"
