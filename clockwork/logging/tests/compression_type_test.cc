// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/compression_type.hh"

#include <catch2/catch_test_macros.hpp>

#include <sstream> // IWYU pragma: keep
#include <string>

namespace clockwork_logging
{
namespace
{

TEST_CASE("Compression type operator<<")
{
  std::stringstream sstream;
  sstream << CompressionType::none; // NOLINT(cert-err33-c) False positive
  CHECK(sstream.str() == "none");
}

} // namespace
} // namespace clockwork_logging
