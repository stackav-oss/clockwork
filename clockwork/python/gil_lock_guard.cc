// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/python/gil_lock_guard.hh"

#include <Python.h>

namespace clockwork::python
{

GilLockGuard::GilLockGuard()
  : gil_state_(PyGILState_Ensure())
{
}

GilLockGuard::~GilLockGuard()
{
  PyGILState_Release(gil_state_);
}

} // namespace clockwork::python
