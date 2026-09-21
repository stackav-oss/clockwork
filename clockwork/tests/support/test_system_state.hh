// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/memory/memory_resource.hh"

namespace clockwork::tests
{
struct SinkState
{
  explicit SinkState(jewels::memory::MemoryResource /*memory_resource*/) {}
};
} // namespace clockwork::tests
