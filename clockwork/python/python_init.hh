// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace clockwork::python
{

/// Initialize the python interpreter if it has not been initialized already
/// @requires The caller must not be holding the global interpreter lock
void python_init_once();

} // namespace clockwork::python
