// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/message_encoding.hh"
#include "clockwork/repr_iface.hh"

#include <boost/core/demangle.hpp>
#include <fmt10/core.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <type_traits> // IWYU pragma: keep
#include <typeinfo>

namespace clockwork_logging
{

template <typename T, typename LoggedMessageType>
void deserialize_tachyon(T& output, const LoggedMessageType& msg)
  requires clockwork::TappyType<T>
{
  if (sizeof(T) != msg.data.size_bytes())
  {
    throw std::runtime_error(fmt::format(
      "Message size ({}) doesn't match output size ({}). {}",
      msg.data.size_bytes(),
      sizeof(T),
      boost::core::demangle(typeid(T).name())));
  }
  std::memcpy(&output, msg.data.data(), sizeof(T));
}

} // namespace clockwork_logging
