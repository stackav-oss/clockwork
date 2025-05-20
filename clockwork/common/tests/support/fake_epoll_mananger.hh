// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/common/abstract_epoll_manager.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_shared_ptr.hh"
#include "jewels/std/expected.hh"

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace clockwork::testing
{
///
/// A minimal implementation of AbstractEPollManager
///
class FakeEPollManager : public AbstractEPollManager
{
public:
  [[nodiscard]] jewels::expected<void, jewels::filesystem::ErrorCode>
  add(int /*efd*/, uint32_t /*events*/, const std::shared_ptr<AbstractEPollCallback>& /*callback*/) override
  {
    return {};
  }

  void remove(int /*efd*/) override {}

  [[nodiscard]] jewels::expected<void, jewels::filesystem::ErrorCode>
  wait(std::chrono::milliseconds /*timeout*/) override
  {
    return {};
  }
};

} // namespace clockwork::testing
