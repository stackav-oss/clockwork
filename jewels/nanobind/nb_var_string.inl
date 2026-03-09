// IWYU pragma: private, include "jewels/nanobind/nb_var_string.hh"
#pragma once

#include "jewels/container/tap/var_string.hh"

#include <fmt/format.h>
#include <nanobind/nanobind.h>
#include <nanobind/stl/string_view.h> // IWYU pragma: keep

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

NAMESPACE_BEGIN(NB_NAMESPACE)
NAMESPACE_BEGIN(detail)

template <size_t fixed_capacity>
struct type_caster<jewels::tap::VarString<fixed_capacity>>
{
  using VarStringT = jewels::tap::VarString<fixed_capacity>;
  NB_TYPE_CASTER(VarStringT, type_caster<std::string_view>::Name)

  bool from_python(handle src, uint8_t flags, cleanup_list* cleanup)
  {
    make_caster<std::string_view> string_caster;
    if (!string_caster.from_python(src, flags_for_local_caster<std::string_view>(flags), cleanup))
    {
      return false;
    }
    const auto& string_val = static_cast<const std::string_view&>(string_caster);

    if (!value.try_set(string_val))
    {
      const std::string error_msg = fmt::format(
        "Capacity is insufficient to convert a python string with length {} to a VarString<{}>.",
        string_val.size(),
        fixed_capacity);
      throw std::length_error(error_msg);
    }
    return true;
  }

  static handle from_cpp(const VarStringT& var_string_val, rv_policy policy, cleanup_list* cleanup)
  {
    return type_caster<std::string_view>::from_cpp(var_string_val.string_view(), policy, cleanup);
  }
};

NAMESPACE_END(detail)
NAMESPACE_END(NB_NAMESPACE)
