// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/uuid/uuid.hh"

#include <xxh3.h>

namespace jewels
{

/// Hasher for UUID
/// @tparam TagType Tag type
template <typename TagType>
struct UuidHasher
{
  size_t operator()(const jewels::Uuid<TagType>& uuid) const
  {
    return XXH3_64bits(uuid.uuid.data(), uuid.uuid.size());
  }
};

} // namespace jewels
