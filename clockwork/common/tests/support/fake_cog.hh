// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/common/abstract_cog.hh"
#include "clockwork/common/forward.hh"
#include "clockwork/common/process_description_clk_cc.hh"
#include "jewels/container/tap/var_array.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/filesystem/file.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/testing/tmp_directory_guard.hh"
#include "jewels/uuid/uuid.hh"

#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <memory>
#include <memory_resource>
#include <span>
#include <unistd.h>

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

  jewels::expected<void, CogExecutionError> prepare_for_execution(jewels::time::SyncTime /*current_time*/) override
  {
    return jewels::unexpected(CogExecutionError::not_ready);
  }
  jewels::expected<void, CogExecutionError> execute(CogExecuteParams /*params*/) override
  {
    return {};
  }
};

} // namespace clockwork::testing
