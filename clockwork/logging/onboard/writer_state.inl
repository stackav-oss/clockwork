// IWYU pragma: private, include "clockwork/logging/onboard/writer_state.hh"
#pragma once

#include "clockwork/logging/onboard/writer_state.hh"

#include <wise_enum.h>

#include <ostream>
#include <string_view>

namespace clockwork_logging::onboard
{

std::ostream& operator<<(std::ostream& ostream, WriterState value)
{
  ostream << wise_enum::to_string(value);
  return ostream;
}

} // namespace clockwork_logging::onboard
