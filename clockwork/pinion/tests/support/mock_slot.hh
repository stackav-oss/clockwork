// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/pinion/slot.hh"

#include <boost/atomic/atomic_ref.hpp>

#include <array>
#include <cstddef>
#include <iterator>
#include <limits>

namespace clockwork::pinion::support
{

/// Mockup of the underlying slot memory layout.
template <size_t message_bytes>
struct alignas(Slot::slot_alignment) SlotStorage
{
  alignas(Slot::header_alignment) Header header{};
  alignas(Slot::message_alignment) std::array<std::byte, message_bytes> message{};
};

} // namespace clockwork::pinion::support
