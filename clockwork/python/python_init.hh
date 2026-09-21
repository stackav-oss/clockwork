// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include <wise_enum.h>

#include <cstdint>

#pragma once

namespace clockwork::python
{

/// Initialzation mode
WISE_ENUM_CLASS(
  (InitializationMode, uint8_t),
  // Production
  production,
  // Unit test
  unit_test)

/// Initialize the python interpreter.
///
/// This function should be called from the main thread before calling any python code.
///
/// @param[in] mode Initialization mode
/// @throws runtime_error If the python interpreter has already been initialized and mode is not unit_test.
void python_init(InitializationMode mode = InitializationMode::production);

/// Verify that the python interpreter has been initialized.
/// @throws runtime_error If the python interpreter has not been initialized.
void throw_if_not_initialized();

} // namespace clockwork::python
