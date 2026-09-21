// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <Python.h>

namespace clockwork::python
{

/// RAII wrapper for the global interpreter lock
/// @requires The python interpreter has been initialized
class GilLockGuard
{
public:
  /// Constructor locks the global interpreter lock
  GilLockGuard();

  /// Destructor unlocks the global interpreter lock
  ~GilLockGuard();

  GilLockGuard(const GilLockGuard& other) = delete;
  GilLockGuard& operator=(const GilLockGuard& other) = delete;
  GilLockGuard(GilLockGuard&&) noexcept = delete;
  GilLockGuard& operator=(GilLockGuard&&) noexcept = delete;

private:
  PyGILState_STATE gil_state_;
};

} // namespace clockwork::python
