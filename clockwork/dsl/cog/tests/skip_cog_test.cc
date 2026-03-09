// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/dsl/cog/tests/support/skip_cog_clk_cc.hh"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <optional>

namespace clockwork
{
TEST_CASE("Skip Threshold Flag")
{
  STATIC_CHECK_FALSE(testing::SkipCogPolicy::InputAPolicy::skip_threshold);
  STATIC_CHECK(testing::SkipCogPolicy::InputBPolicy::skip_threshold == std::optional<size_t>{5U});
}
} // namespace clockwork
