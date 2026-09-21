// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "clockwork/repr_iface.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_unique_ptr.hh"

#include <nanobind/nanobind.h>

#include <string>

namespace jewels
{
/// This is the nanobind support for converting clockwork python types to clockwork C++ clockwork::Tappy<> types.
/// Currently, there is no automatic conversion such as nanobind::type_caster.
/// Rather, the C++ function parameters and return values are nanobind::handle.
/// The conversion functions here convert between nanobind::handle and clockwork::Tappy<> types.
///
/// That means that for every function or method you want to bind, you should write a wrapper function that takes
/// nanobind::handles and manually converts to clockwork::Tappy<> types.
/// See 'kits/nanobind/tests/support/nb_tappy_nanobind_conversion_binding.cc' for an example.

/// Convert a clockwork native python object to a C++ clockwork::Tappy type.
template <typename SchemaT>
clockwork::Tappy<SchemaT> deserialize_tappy_from_py(const nanobind::handle& python_object);

/// Convert a clockwork native python object to a C++ clockwork::Tappy type wrapped in pmr_unique_ptr.
template <typename SchemaT>
jewels::memory::pmr_unique_ptr<clockwork::Tappy<SchemaT>>
deserialize_tappy_ptr_from_py(const nanobind::handle& python_object, jewels::memory::MemoryResource memory_resource);

/// Convert a C++ clockwork::Tappy type to a clockwork native python object.
template <typename SchemaT>
nanobind::handle serialize_tappy_to_py(
  const clockwork::Tappy<SchemaT>& message, const std::string& clkpy_module_name, const std::string& clkpy_class_name);

/// Convert a clockwork python enum to a C++ wise enum.
template <typename EnumT>
bool deserialize_enum_from_py(const nanobind::handle& python_object, EnumT& value);

/// Convert C++ wise enum to a clockwork python enum.
template <typename EnumT>
nanobind::handle
serialize_enum_to_py(EnumT message, const std::string& clkpy_module_name, const std::string& clkpy_class_name);

} // namespace jewels

#include "jewels/nanobind/nanobind_tappy_convert.inl"
