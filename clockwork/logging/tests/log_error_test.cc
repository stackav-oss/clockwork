// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/log_error.hh"

#include <catch2/catch_test_macros.hpp>

#include <sstream> // IWYU pragma: keep
#include <string>

namespace clockwork_logging
{
namespace
{

TEST_CASE("operator<<")
{
  std::stringstream sstream;
  sstream << LogError::out_of_memory; // NOLINT(cert-err33-c) False positive
  CHECK(sstream.str() == "out_of_memory");
}

TEST_CASE("LogExpected")
{
  LogExpected<int32_t> good_exp(1);
  CHECK(good_exp);
  CHECK(good_exp.has_value());
  CHECK(*good_exp == 1);
  CHECK(good_exp.value() == 1);
  CHECK(good_exp == 1);
  CHECK(good_exp != jewels::unexpected(LogError::out_of_memory));

  LogExpected<int32_t> bad_exp(jewels::unexpect, LogError::out_of_memory);
  CHECK_FALSE(bad_exp);
  CHECK_FALSE(bad_exp.has_value());
  CHECK(bad_exp.error() == LogError::out_of_memory);
  CHECK(bad_exp == jewels::unexpected(LogError::out_of_memory));
  CHECK(bad_exp != 1);
}

} // namespace
} // namespace clockwork_logging
