// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/log_cerr/log_cerr.hh"

#include <catch2/catch_test_macros.hpp>

#include <iostream>

namespace jewels
{

TEST_CASE("smoke_test")
{
  std::cerr << "--- normal text ---\n";
  log_cerr_debug("Debug message");
  log_cerr_info("Info message");
  log_cerr_warn("Warning message");
  log_cerr_error("Error message");
  log_cerr_fatal("Fatal error message");
  std::cerr << "--- normal text ---\n";
}

} // namespace jewels
