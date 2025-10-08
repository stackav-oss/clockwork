// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/dsl/cog/tests/support/copy_cog.hh"

#include <catch2/catch_test_macros.hpp>
namespace clockwork
{
TEST_CASE("Copy Inputs Flag")
{
  STATIC_CHECK_FALSE(testing::CopyCogPolicy::InputAPolicy::copy_inputs);
  STATIC_CHECK(testing::CopyCogPolicy::InputBPolicy::copy_inputs);
}
} // namespace clockwork
