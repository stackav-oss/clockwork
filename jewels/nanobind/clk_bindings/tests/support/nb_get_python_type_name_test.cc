// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

// This is really a test, but the runner the shim py_binary //jewels/nanobind/tests:get_python_type_test.py that just
// imports this extension.

#include "jewels/nanobind/clk_bindings/get_python_type_name.hh"

#include <nanobind/nanobind.h>
#include <nanobind/stl/bind_vector.h>
#include <nanobind/stl/optional.h> // IWYU pragma: keep
#include <nanobind/stl/tuple.h>    // IWYU pragma: keep
#include <nanobind/stl/variant.h>  // IWYU pragma: keep

#include <cstdint>
#include <optional>
#include <sstream> // // IWYU pragma: keep - false positive
#include <stdexcept>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

struct Foo
{
};

// Simple assert macro. We can't use catch2 because it isn't designed to run in a python extension, triggered upon
// importing that extension.
// Must use varargs to support multiline strings in messages.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage) Substitute for unit test framework macros.
#define ASSERT(condition, ...) \
  if (!(condition)) \
  { \
    std::ostringstream oss; \
    oss << "Assertion failed at " << __FILE__ << ":" << __LINE__ << ": "; \
    oss << __VA_ARGS__; \
    throw std::runtime_error(oss.str()); \
  }

// Type checker macro that exercises python_type_name and compares against an expected result.
// Must use varargs to support commas in type names.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage) Substitute for unit test framework macros.
#define CHECK_TYPE(expected_str, ...) \
  { \
    const std::string type_name = python_type_name<__VA_ARGS__>(); \
    ASSERT( \
      type_name == expected_str, \
      "python_type_name<" #__VA_ARGS__ ">() returned: '" << type_name << "', expected '" << expected_str << "'"); \
  }

using jewels::nanobind::python_type_name;

NB_MODULE(nb_get_python_type_name_test, py_module)
{
  namespace nb = nanobind;
  nb::class_<Foo>(py_module, "Foo").def(nb::init<>());
  nb::bind_vector<std::vector<Foo>>(py_module, "FooVector");
  nb::bind_vector<std::vector<int32_t>>(py_module, "Int32Vector");

  CHECK_TYPE("jewels.nanobind.clk_bindings.tests.support.nb_get_python_type_name_test.Foo", Foo);
  CHECK_TYPE("jewels.nanobind.clk_bindings.tests.support.nb_get_python_type_name_test.FooVector", std::vector<Foo>);
  CHECK_TYPE(
    "jewels.nanobind.clk_bindings.tests.support.nb_get_python_type_name_test.Int32Vector", std::vector<int32_t>);
  CHECK_TYPE("int | None", std::optional<int32_t>);
  CHECK_TYPE("jewels.nanobind.clk_bindings.tests.support.nb_get_python_type_name_test.Foo | None", std::optional<Foo>);
  CHECK_TYPE(
    "jewels.nanobind.clk_bindings.tests.support.nb_get_python_type_name_test.Int32Vector | None",
    std::optional<std::vector<int32_t>>);
  CHECK_TYPE(
    "jewels.nanobind.clk_bindings.tests.support.nb_get_python_type_name_test.Foo | "
    "jewels.nanobind.clk_bindings.tests.support.nb_get_python_type_name_test.Int32Vector",
    std::variant<Foo, std::vector<int32_t>>);
  CHECK_TYPE(
    "tuple[jewels.nanobind.clk_bindings.tests.support.nb_get_python_type_name_test.Foo, "
    "jewels.nanobind.clk_bindings.tests.support.nb_get_python_type_name_test.Int32Vector]",
    std::tuple<Foo, std::vector<int32_t>>);
}
