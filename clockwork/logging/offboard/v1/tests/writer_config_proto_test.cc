// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/offboard/v1/writer_config.pb.h"
#include "jewels/testing/wise_enum_proto_sync.hh"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace clockwork_logging
{
namespace
{

TEST_CASE("CompressionType")
{
  REQUIRE_WISE_ENUM_PROTO_SYNC(CompressionType, clockwork::logging::offboard::v1::CompressionType);
}

} // namespace
} // namespace clockwork_logging
