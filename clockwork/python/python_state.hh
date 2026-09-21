// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/python/python_object.hh"
#include "jewels/memory/memory_resource.hh"

namespace clockwork::python
{

/// C++ state common to all python cogs
class PythonState
{
public:
  /// Constructor
  /// @param[in] memory_resource Memory resource
  explicit PythonState(jewels::memory::MemoryResource memory_resource);

  /// Destructor releases the python state dictionary.
  ~PythonState();

  PythonState(const PythonState& other) = delete;
  PythonState& operator=(const PythonState& other) = delete;
  PythonState(PythonState&&) noexcept = default;
  PythonState& operator=(PythonState&&) noexcept = default;

  /// Python state dictionary accessor
  /// @requires The caller must be holding the global interpreter lock
  /// @return Python dictionary object
  [[nodiscard]] const PythonObject& get_python_state_dictionary();

private:
  /// Dictionary of state available to the python cog
  PythonObject python_state_dictionary_;
};

} // namespace clockwork::python
