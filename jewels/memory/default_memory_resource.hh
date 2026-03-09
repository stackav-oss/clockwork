// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

/// Convenience function to grab a default MemoryResource for use in tests.
#pragma once

#include "jewels/memory/memory_resource.hh"

namespace jewels::memory
{
/// Create a MemoryResource that uses the default new/delete memory resource under the hood.
jewels::memory::MemoryResource get_default_memory_resource() noexcept;
} // namespace jewels::memory
