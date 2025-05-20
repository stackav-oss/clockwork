// IWYU pragma: private, include "jewels/container/tap/tap_to_protobuf.hh"
#pragma once

#include "jewels/container/tap/tap_to_protobuf.hh"

#include "jewels/container/tap/var_array.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/uuid/uuid.hh"

#include <cstddef>
#include <cstring>
#include <span>
#include <string>

namespace jewels
{

template <typename Tag>
void tap_to_protobuf(std::string& output, const Uuid<Tag>& input)
{
  output = input.to_string();
}

template <size_t capacity>
void tap_to_protobuf(std::string& output, const tap::VarString<capacity>& input)
{
  output = input.string_view();
}

template <size_t capacity>
void tap_to_protobuf(std::string& output, const tap::VarArray<std::byte, capacity>& input)
{
  output.resize(input.size());
  if (!input.empty())
  {
    std::memcpy(output.data(), input.data(), input.size());
  }
}

template <size_t size>
void tap_to_protobuf(std::string& output, std::span<const std::byte, size> input)
{
  output.resize(input.size());
  if (!input.empty())
  {
    std::memcpy(output.data(), input.data(), input.size());
  }
}
} // namespace jewels
