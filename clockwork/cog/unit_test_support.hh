// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/pinion/publishable.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/rate_limiter/token_bucket.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <sys/types.h>
#include <tuple>
#include <utility>
#include <variant>

namespace clockwork
{

/// Forward declaration
template <typename PolicyT>
class InputView;

namespace testing
{

/// Policy used by unit test cogs to define views for the messages published by a unit test cog
/// @tparam MsgTypeT Message type
/// @tparam endpoint_id_v Endpoint UUID
template <typename MsgTypeT, typename PolicyT>
struct UnitTestCogOutputViewPolicy
{
  using MsgType = MsgTypeT;
  static constexpr auto endpoint_id = PolicyT::endpoint_id;
  static constexpr auto name = PolicyT::name;
  static constexpr uint32_t max_view_size = 1U;
  static constexpr uint32_t min_msgs = 0U;
  static constexpr uint32_t min_new_msgs = 0U;
  static constexpr std::optional<::ssize_t> safety_margin = -1;
  static constexpr std::optional<size_t> skip_threshold = std::nullopt;
  static constexpr auto copy_inputs = false;
  static constexpr auto manual_cursor = false;
};

/// Unit test cog output view pointer type
/// @tparam OutputViewPolicy Output view policy
template <typename OutputViewPolicy>
using UnitTestCogOutputViewPtrType = std::shared_ptr<InputView<OutputViewPolicy>>;

} // namespace testing

} // namespace clockwork
