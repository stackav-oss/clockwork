// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/onboard/log_format.hh"

#include <catch2/catch_test_macros.hpp>

#include <sstream> // IWYU pragma: keep
#include <string>

namespace clockwork_logging::onboard
{
namespace
{

TEST_CASE("Record type operator<<")
{
  std::stringstream sstream;
  sstream << RecordType::message; // NOLINT(cert-err33-c) False positive
  CHECK(sstream.str() == "message");
}

} // namespace
} // namespace clockwork_logging::onboard
