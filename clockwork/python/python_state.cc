// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/python/python_state.hh"

#include "clockwork/python/gil_lock_guard.hh"
#include "clockwork/python/python_object.hh"

#include <stdexcept>
#include <string>

namespace clockwork::python
{

PythonState::PythonState(jewels::memory::MemoryResource /*memory_resource*/) {}

PythonState::~PythonState()
{
  const GilLockGuard gil_guard;
  python_state_dictionary_.reset();
}

const PythonObject& PythonState::get_python_state_dictionary()
{
  if (!python_state_dictionary_)
  {
    python_state_dictionary_ = PythonObject::make_dictionary();
    if (!python_state_dictionary_)
    {
      throw std::runtime_error(get_python_error());
    }
  }
  return python_state_dictionary_;
}

} // namespace clockwork::python
