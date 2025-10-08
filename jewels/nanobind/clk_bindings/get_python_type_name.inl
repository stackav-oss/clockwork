// IWYU pragma: private, include "jewels/nanobind/clk_bindings/get_python_type_name.hh"
#pragma once

#include "jewels/nanobind/clk_bindings/get_python_type_name.hh"

#include <nanobind/nanobind.h>

#include <stdexcept>
#include <string>
#include <string_view>

namespace jewels::nanobind
{

// There isn't a great way to get the python type from a C++ type.
// The obvious way would be to try nb::type<T>() for bound types or make_caster<T>::Name for type casters,
// but it turns out that these approaches don't mix, so optional<BoundType> can't be recovered.
//
// Instead, we construct the type by hijacking the internal mechanism nanobind uses - we construct a function returning
// this type and parse its type signature.
//
// See https://github.com/wjakob/nanobind/discussions/743 for the discussion.
template <typename T>
std::string python_type_name()
{
  namespace nb = ::nanobind;

  // Create a nanobind function object returning type T
  auto fun = nb::cpp_function([]() -> T* { return {}; });
  const nb::object signature_obj = nb::getattr(fun, "__nb_signature__", nb::none());

  if (signature_obj.is_none())
  {
    throw std::runtime_error("Function object does not have a '__nb_signature__' attribute.");
  }

  // Ok, the signature is wrapped in tuples. It looks like (('def () -> Foo', None, None),)
  // In python we'd call signature[0][0] but in C++ we should be more careful and type check at each step, so that
  // if something goes wrong we get a better error message than the dreaded "nb::bad_cast" exception.
  //
  // Consider the rest of this a sanity-checked version of:
  // > parse_type_from_function_signature(str(cast<tuple>(cast<tuple>(signature)[0])[0]))

  // Make sure signature_obj is a tuple
  if (!nb::isinstance<nb::tuple>(signature_obj))
  {
    throw std::runtime_error("Attribute '__nb_signature__' is not of expected type (tuple).");
  }

  // Cast signature_obj to nb::tuple.
  const auto outer_tuple = nb::cast<nb::tuple>(signature_obj);

  // Make sure the outer tuple has at least one element.
  if (outer_tuple.size() < 1)
  {
    throw std::runtime_error("signature_obj tuple is empty.");
  }

  // Get the first element of the outer tuple.
  const nb::object inner_obj = outer_tuple[0];

  // Make sure inner_obj is a tuple.
  if (!nb::isinstance<nb::tuple>(inner_obj))
  {
    throw std::runtime_error("First element of signature_obj is not a tuple.");
  }

  // Cast inner_obj to nb::tuple.
  const auto inner_tuple = nb::cast<nb::tuple>(inner_obj);

  // Make sure inner_tuple has at least one element.
  if (inner_tuple.size() < 1)
  {
    throw std::runtime_error("Inner tuple is empty.");
  }

  // Get the first element of the inner tuple.
  const nb::object signature_str_obj = inner_tuple[0];

  return ::jewels::nanobind::detail::parse_type_from_function_signature(
    std::string_view(nb::str(signature_str_obj).c_str()));
}

} // namespace jewels::nanobind
