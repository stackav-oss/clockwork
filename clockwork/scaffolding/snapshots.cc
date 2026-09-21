// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/scaffolding/snapshots.hh"

#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/scaffolding/abstract_casing.hh"
#include "jewels/callsig/outcome.hh"

#include <span>

namespace clockwork::scaffolding
{

jewels::BinaryOutcome
setup_snapshot_configs(std::span<const Tappy<common::SnapshotConfig>> snapshot_configs, AbstractCasing& casing)
{
  for (const auto& snapshot_config : snapshot_configs)
  {
    auto result = casing.try_configure_snapshot(snapshot_config);
    if (jewels::fails(result))
    {
      return jewels::failure;
    }
  }
  return jewels::success;
}

} // namespace clockwork::scaffolding
