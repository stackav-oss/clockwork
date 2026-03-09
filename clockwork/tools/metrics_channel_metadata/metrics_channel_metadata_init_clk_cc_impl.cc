// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/pinion/publishable.hh"
#include "clockwork/tools/metrics_channel_metadata/metrics_channel_metadata_config_clk_cc.hh"
#include "clockwork/tools/metrics_channel_metadata/metrics_channel_metadata_init_clk_cc_dial.hh"
#include "jewels/container/tap/var_array.hh"

namespace clockwork::tools
{
void execute_cog(MetricsChannelMetadataInitCogDial& dial)
{
  const auto& metrics_config = dial.get_configs().get_metrics_config();
  dial.get_outputs().get_metrics_channel_metadata().message().get_underlying_metrics_channels() =
    metrics_config.get_underlying_metrics_channels();

  dial.get_outputs().get_metrics_channel_metadata().mark_for_publish();
}
} // namespace clockwork::tools
