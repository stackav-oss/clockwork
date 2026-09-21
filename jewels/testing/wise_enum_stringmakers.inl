// IWYU pragma: private, include "jewels/testing/wise_enum_stringmakers.hh"
#pragma once

#include "jewels/testing/wise_enum_stringmakers.hh"

#include <catch2/catch_tostring.hpp>
#include <wise_enum.h>

#include <string>

namespace Catch
{
template <WiseEnum EnumType>
std::string StringMaker<EnumType>::convert(const EnumType& value)
{
  return std::string(wise_enum::to_string(value));
}
} // namespace Catch
