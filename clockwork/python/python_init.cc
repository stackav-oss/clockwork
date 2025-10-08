// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/python/python_init.hh"

#include <Python.h>

#include <stdexcept>

namespace clockwork::python
{

void python_init(InitializationMode mode)
{
  if (Py_IsInitialized() != 0)
  {
    if (mode != InitializationMode::unit_test)
    {
      throw std::runtime_error("The python interpreter is already initialized.");
    }
    return;
  }
  Py_InitializeEx(0);
  PyEval_SaveThread();
}

void throw_if_not_initialized()
{
  if (Py_IsInitialized() == 0)
  {
    throw std::runtime_error("The python interpreter has not been initialized.");
  }
}

} // namespace clockwork::python
