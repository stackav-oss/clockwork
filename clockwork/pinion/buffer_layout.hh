
// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/pinion/aligned_pointer.hh"
#include "clockwork/pinion/buffer_index.hh"
#include "clockwork/pinion/buffer_layout.hh"
#include "clockwork/pinion/error.hh"
#include "jewels/math/power_of_two.hh"
#include "jewels/std/expected.hh"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>

namespace clockwork::pinion
{
/// Describes the layout of the buffer.
struct BufferLayout
{
  /// Size of the head index in bytes.
  static constexpr auto head_size{sizeof(BufferIndex)};
  /// Size of the tail index in bytes.
  static constexpr auto tail_size{sizeof(BufferIndex)};
  /// Size of the entire control block that trails the last slot.
  static constexpr auto control_block_size{head_size + tail_size};

  /// Alignment of the head index.
  static constexpr auto head_alignment{std::atomic_ref<BufferIndex>::required_alignment};
  /// Alignment of the tail index.
  static constexpr auto tail_alignment{std::atomic_ref<BufferIndex>::required_alignment};

  /// Number of slots in the buffer.
  size_t num_slots{};
  /// Size of the message payload.
  size_t message_size{};
  /// True if the channel is only published once
  bool is_published_once{};
  /// Maximum number of messages that may be committed in a single execution cycle.
  size_t max_msgs_per_exec{1};
};

/// Get the size of the buffer as a multiple of the alignment.
/// @param layout Describes the layout of the buffer.
constexpr size_t slot_stride(const BufferLayout& layout) noexcept;

/// Get the size of the buffer in total bytes.
/// @param layout Describes the layout of the buffer.
constexpr size_t buffer_size(const BufferLayout& layout) noexcept;

/// Get the offset to the head index in the control block.
/// @param layout Describes the layout of the buffer.
constexpr size_t head_offset(const BufferLayout& layout) noexcept;

/// Get the offset to the tail index in the control block.
/// @param layout Describes the layout of the buffer.
constexpr size_t tail_offset(const BufferLayout& layout) noexcept;
} // namespace clockwork::pinion

#include "clockwork/pinion/buffer_layout.inl"
