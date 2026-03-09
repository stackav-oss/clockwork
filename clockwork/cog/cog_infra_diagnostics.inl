// IWYU pragma: private, include "clockwork/cog/cog_infra_diagnostics.hh"
#pragma once

#include "clockwork/cog/cog_infra_diagnostics.hh"

#include "clockwork/cog/cog_diagnostics.hh"
#include "clockwork/common/process_description_clk_cc.hh"
#include "jewels/uuid/uuid.hh"

namespace clockwork
{

template <typename Policy>
CogInfraDiagnostics<Policy>::CogInfraDiagnostics(const jewels::Uuid<common::CogInstanceId>& instance_id) noexcept
  : CogDiagnosticsImpl<CogInfraDiagnosticsLazyPolicyWrapper<Policy>>(instance_id)
{
}

} // namespace clockwork
