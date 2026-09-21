// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "clockwork/repr_iface.hh"
#include "clockwork/serializable.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/memory/memory_resource.hh"

namespace clockwork::demo
{
struct HelloMsg;
}

namespace clockwork::testing
{
struct CxxState
{
  explicit CxxState(jewels::memory::MemoryResource /*memres*/) {}
};
} // namespace clockwork::testing

namespace clockwork
{
template <>
struct Serializable<testing::CxxState>
{
  using SerializedType = Tappy<demo::HelloMsg>;

  static jewels::BinaryOutcome serialize(jewels::Out<SerializedType> snapshot, const testing::CxxState& state);

  static jewels::BinaryOutcome deserialize(jewels::Out<testing::CxxState> state, const SerializedType& snapshot);
};
} // namespace clockwork
