// IWYU pragma: private, include "clockwork/logging/onboard/log_format.hh"
#pragma once

#include "clockwork/logging/onboard/log_format.hh"

#include <wise_enum.h>

#include <ostream>
#include <string_view>

namespace clockwork_logging::onboard
{

std::ostream& operator<<(std::ostream& ostream, RecordType value)
{
  ostream << wise_enum::to_string(value);
  return ostream;
}

} // namespace clockwork_logging::onboard
