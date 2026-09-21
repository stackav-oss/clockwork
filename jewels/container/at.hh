// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <gsl/util> // IWYU pragma: export

namespace jewels
{
// Note that some classes may need to declare friendship with at to allow use of jewels::at.
// In that case, friendship must be declared against the actual function, and not this using declaration, which will
// require adding an allowance in disallow_added_strings_allowances.yaml. Use PER-6266 for the ticket in the allowance.
using gsl::at; // NOLINT(misc-unused-using-decls) needed to bring at out of the gsl namespace
} // namespace jewels
