// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/signal_metadata_config_clk_cc.hh"
#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/publishable.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/tools/signal_metadata/signal_metadata_init_clk_cc_dial.hh"
#include "jewels/uuid/uuid.hh"

namespace clockwork::tools
{
void execute_cog(SignalMetadataInitCogDial& dial)
{
  dial.get_outputs().get_signal_metadata().message() = dial.get_configs().get_signal_metadata_config();
  dial.get_outputs().get_signal_metadata().mark_for_publish();
}
} // namespace clockwork::tools
