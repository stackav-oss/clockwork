// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/pinion/slot_ref.hh"

#include "clockwork/pinion/aligned_pointer.hh"
#include "jewels/math/power_of_two.hh"

namespace clockwork::pinion
{

Slot detail::MonostateSlotRefReader::get_empty_slot()
{
  // Following the behavior of BufferIterator::dereference on the sentinel interator, this just returns an invalid Slot
  static struct alignas(Slot::slot_alignment) DefaultData
  {
    // Allows a default constructor for the iterator.
  } default_data;
  return Slot{AlignedBytePtr<Slot::slot_alignment>{AlignedBytePtr<Slot::slot_alignment>::from_ref(default_data)}, 0};
}

} // namespace clockwork::pinion
