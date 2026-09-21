// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <sys/types.h>

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
  static constexpr auto expose_seqno = false;
  static constexpr auto use_device_ptr = false;
};

/// Unit test cog output view pointer type
/// @tparam OutputViewPolicy Output view policy
template <typename OutputViewPolicy>
using UnitTestCogOutputViewPtrType = std::shared_ptr<InputView<OutputViewPolicy>>;

} // namespace testing

} // namespace clockwork
