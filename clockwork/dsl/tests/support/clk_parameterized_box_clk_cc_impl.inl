// IWYU pragma: private, include "clockwork/dsl/tests/support/clk_parameterized_box_clk_cc_impl.hh"

#pragma once

// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/dsl/tests/support/clk_parameterized_box_clk_cc_impl.hh"

#include "clockwork/dsl/tests/support/clk_parameterized_box_clk_cc_dial.hh"

namespace clockwork::testing
{

template <ParamTestCogDialType DialType>
void execute_cog(DialType& /*dial*/)
{
}

} // namespace clockwork::testing
