// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/repr_iface.hh"

#include <boost/core/demangle.hpp>
#include <fmt/format.h>

#include <cstddef>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits> // IWYU pragma: keep
#include <typeinfo>

namespace clockwork_logging
{

template <typename T>
void deserialize_tachyon(T& output, std::span<const std::byte> data)
  requires clockwork::TappyType<T>
{
  if (sizeof(T) != data.size_bytes())
  {
    throw std::runtime_error(
      fmt::format(
        "Message size ({}) doesn't match output size ({}). {}",
        data.size_bytes(),
        sizeof(T),
        boost::core::demangle(typeid(T).name())));
  }
  std::memcpy(&output, data.data(), sizeof(T));
}

} // namespace clockwork_logging
