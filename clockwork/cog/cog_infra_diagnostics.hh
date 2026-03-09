// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/cog/cog_diagnostics.hh"
#include "clockwork/cog/unit_test_support.hh"
#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/diagnostics/report_clk_cc.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <cstddef>
#include <tuple>

namespace clockwork
{

/// Helper to resolve the lazy-defined ManagerType used for infra diags to a normal non-lazy type
/// This is needed because the Group is defined in the Policy so isn't a complete type when the
/// Manager alias is created.  At this point, however, the Manager can be concretely instantiated.
template <typename Policy>
struct CogInfraDiagnosticsLazyPolicyWrapper : Policy
{
  using ManagerType = Policy::template ManagerType<>;
};

/// Helper class to handle cog infrastructure diagnostics
template <typename Policy>
class CogInfraDiagnostics : public CogDiagnosticsImpl<CogInfraDiagnosticsLazyPolicyWrapper<Policy>>
{
public:
  using UnitTestOutputViewPolicyType = testing::UnitTestCogOutputViewPolicy<Tappy<diagnostics::Report>, Policy>;

  /// Construct the diagnostics helper.
  /// @param[in] instance_id The id of the cog instance, to be used as the reporter id
  explicit CogInfraDiagnostics(const jewels::Uuid<common::CogInstanceId>& instance_id) noexcept;

private:
};

} // namespace clockwork

#include "clockwork/cog/cog_infra_diagnostics.inl"
