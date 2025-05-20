// IWYU pragma: private, include "clockwork/logging/compression_type.hh"
#pragma once

#include "clockwork/logging/compression_type.hh"

#include <wise_enum.h>

#include <ostream>
#include <string_view>

namespace clockwork_logging
{

inline std::ostream& operator<<(std::ostream& ostream, CompressionType value)
{
  ostream << wise_enum::to_string(value);
  return ostream;
}

} // namespace clockwork_logging
