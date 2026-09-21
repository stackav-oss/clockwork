// IWYU pragma: private, include "jewels/testing/expected_stringmakers.hh"
#pragma once

#include "jewels/testing/expected_stringmakers.hh"

#include "jewels/std/expected.hh"

#include <catch2/catch_tostring.hpp>

#include <string>
#include <type_traits>

namespace Catch
{
template <typename Result, typename Error>
std::string StringMaker<jewels::expected<Result, Error>>::convert(const ::jewels::expected<Result, Error>& value)
{
  if constexpr (std::is_same_v<Result, void>)
  {
    if (value.has_value())
    {
      return "void";
    }
  }
  else
  {
    if (value.has_value())
    {
      return StringMaker<Result>::convert(value.value());
    }
  }
  return StringMaker<Error>::convert(value.error());
}

std::string StringMaker<jewels::MonoError>::convert(const jewels::MonoError& /*value*/)
{
  return "MonoError";
}
} // namespace Catch
