// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/dsl/tests/support/cxx_state.hh"

#include "clockwork/dsl/tests/support/hello_msg_onboard.hh"

#include <cstring>

namespace clockwork
{
jewels::BinaryOutcome
Serializable<testing::CxxState>::serialize(jewels::Out<SerializedType> snapshot, const testing::CxxState& /*state*/)
{
  std::memset(&*snapshot, 0, sizeof(SerializedType));
  return jewels::success;
}

jewels::BinaryOutcome Serializable<testing::CxxState>::deserialize(
  jewels::Out<testing::CxxState> /*state*/, const SerializedType& /*snapshot*/)
{
  return jewels::success;
}
} // namespace clockwork
