// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/repr_iface.hh"
#include "clockwork/serializable.hh"
#include "clockwork/tests/support/snapshot_test_messages.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/memory/memory_resource.hh"

#include <cstdint>

namespace clockwork::testing
{
struct SerializableState
{
  explicit SerializableState(jewels::memory::MemoryResource /*memres*/) {}

  int32_t value{0};
};
} // namespace clockwork::testing

namespace clockwork
{
template <>
struct Serializable<testing::SerializableState>
{
  using SerializedType = Tappy<testing::SerializableStateSnapshot>;

  static jewels::BinaryOutcome serialize(jewels::Out<SerializedType> snapshot, const testing::SerializableState& state)
  {
    snapshot->set_value(state.value);
    return jewels::success;
  }

  static jewels::BinaryOutcome
  deserialize(jewels::Out<testing::SerializableState> state, const SerializedType& snapshot)
  {
    if (snapshot.get_value() == -1)
    {
      return jewels::failure;
    }
    state->value = snapshot.get_value();
    return jewels::success;
  }
};
} // namespace clockwork
