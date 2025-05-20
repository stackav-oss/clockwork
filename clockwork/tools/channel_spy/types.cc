// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/tools/channel_spy/types.hh"

#include <cstddef>
#include <cstdint>

namespace clockwork::tools
{

PythonCallbackHandle::PythonCallbackHandle(
  uint64_t sequence_number_in,
  int64_t message_time_in,
  std::span<const std::byte> data_in,
  const pinion::SubscriberHandle& subscriber_handle_in,
  pinion::BufferIterator buffer_iter_in)
  : sequence_number(sequence_number_in),
    message_time(message_time_in),
    data_ptr(jewels::memory::make_non_null_from_ref(*data_in.data())),
    data_size(data_in.size()),
    subscriber_handle(jewels::memory::make_non_null_from_ref(subscriber_handle_in)),
    buffer_iter(buffer_iter_in)
{
}

} // namespace clockwork::tools
