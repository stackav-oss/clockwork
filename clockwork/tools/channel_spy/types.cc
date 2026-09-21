// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/tools/channel_spy/types.hh"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace clockwork::tools
{

PythonCallbackHandle::PythonCallbackHandle(
  uint64_t sequence_number_in, int64_t message_time_in, std::span<const std::byte> data_in, pinion::SlotRef slot_ref_in)
  : sequence_number(sequence_number_in),
    message_time(message_time_in),
    data_ptr(jewels::memory::make_non_null_from_ref(*data_in.data())),
    data_size(data_in.size()),
    slot_ref(std::move(slot_ref_in))
{
}

} // namespace clockwork::tools
