// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/scaffolding/abstract_casing.hh"

namespace clockwork::scaffolding
{

/// Validate that every class UUID referenced by the given process description
/// is linked into this executable.
///
/// Walks the PDF and, for every class-UUID-bearing entry (cog instances, state
/// representations, data source representations, IO connection classes), looks
/// up the UUID against either the global `CogFactory` / `CogStateFactory`
/// linked lists or the casing's compile-time template tuples.  No object is
/// constructed.
///
/// All missing UUIDs are reported in a single pass (no early exit on first
/// failure) so a developer fixing build deps can address them in one pass.
///
/// @param desc The parsed process description file.
/// @param casing The casing constructed via `make_casing`.  Must not have had
///   `setup_*` / `connect_*` / `finalize` called on it.
/// @return `EXIT_SUCCESS` if all UUIDs are linked; `EXIT_FAILURE` otherwise.
[[nodiscard]] int validate_casing(const Tappy<common::ProcessDescription<>>& desc, const AbstractCasing& casing);

} // namespace clockwork::scaffolding
