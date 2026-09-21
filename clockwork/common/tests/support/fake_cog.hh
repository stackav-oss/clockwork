// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/common/abstract_cog.hh"
#include "clockwork/common/forward.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"

#include <memory>
#include <string_view>

namespace clockwork::testing
{

class FakeCog : public AbstractCog
{
public:
  explicit FakeCog(const std::shared_ptr<AbstractCogQueue>& queue)
    : AbstractCog(jewels::memory::make_non_null_from_ref(*queue))
  {
  }
  explicit FakeCog(jewels::memory::ObjectPtr<AbstractCogQueue> queue)
    : AbstractCog(queue)
  {
  }
  [[nodiscard]] std::string_view get_name() const override
  {
    return "clockwork::testing::FakeCog";
  }
  jewels::expected<void, jewels::MonoError> prime(jewels::time::SyncTime /*start_time*/) override
  {
    return {};
  }

  CogPrepareOutcome prepare_for_execution(
    jewels::Out<jewels::time::SyncTime> /*throttled_until_out*/, jewels::time::SyncTime /*current_time*/) override
  {
    return CogPrepareResult::not_ready;
  }
  jewels::expected<void, CogExecutionError> execute(CogExecuteParams /*params*/) override
  {
    return {};
  }
};

} // namespace clockwork::testing
