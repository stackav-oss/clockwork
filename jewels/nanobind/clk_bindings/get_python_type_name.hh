// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <string_view>

namespace jewels::nanobind
{

/// Compute the runtime python class name of a compile-time C++ type using nanobind library.
/// See documentation in get_python_type_name.inl if you really want to see how the sausage is made.
// @tparam T The C++ type for which we compute the python type name.
template <typename T>
std::string python_type_name();

namespace detail
{
// Parse the return type "Foo" from the signature "def () -> Foo".
std::string parse_type_from_function_signature(std::string_view signature);
} // namespace detail

} // namespace jewels::nanobind

#include "jewels/nanobind/clk_bindings/get_python_type_name.inl"
