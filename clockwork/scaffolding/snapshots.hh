// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/scaffolding/abstract_casing.hh"
#include "jewels/callsig/outcome.hh"

#include <span>

namespace clockwork::scaffolding
{

///
/// Connect all snapshot publishers to their respective endpoints
/// @param snapshot_configs list of snapshot configurations
/// @param casing the casing to request connections against
///
[[nodiscard]] jewels::BinaryOutcome
setup_snapshot_configs(std::span<const Tappy<common::SnapshotConfig>> snapshot_configs, AbstractCasing& casing);

} // namespace clockwork::scaffolding
