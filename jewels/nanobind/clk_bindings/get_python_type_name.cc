// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/nanobind/clk_bindings/get_python_type_name.hh"

#include <cstddef>
#include <stdexcept>

namespace jewels::nanobind::detail
{

std::string parse_type_from_function_signature(const std::string_view signature)
{
  // Extract the return type from the signature string.
  // The signature looks like "def () -> Foo" and we're after "Foo".
  const size_t arrow_pos = signature.find("-> ");
  if (arrow_pos == std::string_view::npos)
  {
    throw std::runtime_error("Could not find '->' in signature string.");
  }

  std::string return_type(signature.substr(arrow_pos + 3)); // Skip '-> '
  // Trim any whitespace from the end
  return_type.erase(return_type.find_last_not_of(" \n\r\t") + 1);

  return return_type;
}

} // namespace jewels::nanobind::detail
