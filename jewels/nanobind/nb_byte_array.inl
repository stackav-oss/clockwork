// IWYU pragma: private, include "jewels/nanobind/nb_byte_array.hh"
#pragma once

#include "jewels/container/tap/var_array.hh"
#include "jewels/log_cerr/log_cerr.hh"

#include <nanobind/nanobind.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>

namespace nanobind::detail
{

template <size_t fixed_capacity>
struct type_caster<::jewels::tap::VarArray<std::byte, fixed_capacity>>
{
  using VarArrayT = ::jewels::tap::VarArray<std::byte, fixed_capacity>;
  NB_TYPE_CASTER(VarArrayT, type_caster<nanobind::bytes>::Name)

  bool from_python(handle src, uint8_t flags, cleanup_list* cleanup)
  {
    make_caster<nanobind::bytes> bytes_caster;
    if (!bytes_caster.from_python(src, flags_for_local_caster<nanobind::bytes>(flags), cleanup))
    {
      return false;
    }
    const auto& bytes_val = static_cast<const nanobind::bytes&>(bytes_caster);

    if (value.capacity() < bytes_val.size())
    {
      // Logging an error and returning false is the best we can do
      // here.  The porting documentation states that casters should
      // not throw and should return false on failure.  This also
      // matches the behavior of the std::array caster implemented in
      // Nanobind.
      // Porting docs:
      // https://github.com/wjakob/nanobind/blob/a5701022b461350f8f01132bcfced2106be6108e/docs/porting.rst#type-casters
      // Array caster: https://github.com/wjakob/nanobind/blob/master/include/nanobind/stl/detail/nb_array.h#L19
      ::jewels::log_cerr_error(
        "Capacity is insufficient to convert python bytes with length {} to a VarArray<std::byte, {}>",
        bytes_val.size(),
        fixed_capacity);
      return false;
    }
    value.resize(bytes_val.size());
    const auto* const ptr = static_cast<const std::byte*>(bytes_val.data());
    ::std::ranges::copy(std::span{ptr, bytes_val.size()}, std::begin(value));
    return true;
  }

  static handle from_cpp(const VarArrayT& byte_array_val, rv_policy policy, cleanup_list* cleanup)
  {
    return type_caster<nanobind::bytes>::from_cpp(
      nanobind::bytes{static_cast<const void*>(byte_array_val.data()), byte_array_val.size()}, policy, cleanup);
  }
};

template <size_t size>
struct type_caster<std::array<std::byte, size>>
{
  using ArrayT = std::array<std::byte, size>;
  NB_TYPE_CASTER(ArrayT, type_caster<nanobind::bytes>::Name)

  bool from_python(handle src, uint8_t flags, cleanup_list* cleanup)
  {
    make_caster<nanobind::bytes> bytes_caster;
    if (!bytes_caster.from_python(src, flags_for_local_caster<nanobind::bytes>(flags), cleanup))
    {
      return false;
    }
    const auto& bytes_val = static_cast<const nanobind::bytes&>(bytes_caster);

    if (value.size() != bytes_val.size())
    {
      // Logging an error and returning false is the best we can do
      // here.  The porting documentation states that casters should
      // not throw and should return false on failure.  This also
      // matches the behavior of the std::array caster implemented in
      // Nanobind.
      // Porting docs:
      // https://github.com/wjakob/nanobind/blob/a5701022b461350f8f01132bcfced2106be6108e/docs/porting.rst#type-casters
      // Array caster: https://github.com/wjakob/nanobind/blob/master/include/nanobind/stl/detail/nb_array.h#L19
      ::jewels::log_cerr_error(
        "Size of python bytes {} is not the same as FixedArray<std::byte, {}>", bytes_val.size(), size);
      return false;
    }
    const auto* const ptr = static_cast<const std::byte*>(bytes_val.data());
    ::std::ranges::copy(std::span{ptr, bytes_val.size()}, std::begin(value));
    return true;
  }

  static handle from_cpp(const ArrayT& byte_array_val, rv_policy policy, cleanup_list* cleanup)
  {
    return type_caster<nanobind::bytes>::from_cpp(
      nanobind::bytes{static_cast<const void*>(byte_array_val.data()), byte_array_val.size()}, policy, cleanup);
  }
};

} // namespace nanobind::detail
