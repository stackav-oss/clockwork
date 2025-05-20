// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/uuid/uuid.hh"

#include <array>
#include <cstdint>
#include <string_view>

namespace jewels
{

std::array<uint8_t, uuid_size_bytes>
uuid5_raw(const std::array<uint8_t, uuid_size_bytes>& uuid_ns, std::string_view name);

template <typename TagType, typename NamespaceTag>
Uuid<TagType> uuid5(const Uuid<NamespaceTag>& uuid_ns, std::string_view name)
{
  return Uuid<TagType>(uuid5_raw(uuid_ns.uuid, name));
}

} // namespace jewels
