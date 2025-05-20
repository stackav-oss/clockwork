// IWYU pragma: private, include "jewels/nanobind/nb_au.hh"
#pragma once

#include <au/quantity.hh>
#include <nanobind/nanobind.h>

#include <cstdint>

NAMESPACE_BEGIN(NB_NAMESPACE)
NAMESPACE_BEGIN(detail)

template <typename UnitT, typename RepT>
struct type_caster<au::Quantity<UnitT, RepT>>
{
  using T = au::Quantity<UnitT, RepT>;
  NB_TYPE_CASTER(T, type_caster<RepT>::Name)

  bool from_python(handle src, uint8_t flags, cleanup_list* cleanup) noexcept
  {
    make_caster<RepT> caster;
    if (!caster.from_python(src, flags, cleanup))
    {
      return false;
    }
    value = au::make_quantity<UnitT, RepT>(caster.value);
    return true;
  }

  static handle from_cpp(au::Quantity<UnitT, RepT> val, rv_policy policy, cleanup_list* cleanup) noexcept
  {
    RepT rep = val.in(au::QuantityMaker<UnitT>{});
    return type_caster<RepT>::from_cpp(rep, policy, cleanup);
  }
};

NAMESPACE_END(detail)
NAMESPACE_END(NB_NAMESPACE)
