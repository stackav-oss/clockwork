// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0
// IWYU pragma: private, include "clockwork/serialization/cpp/tests/support/validate_upgradability.hh"
#pragma once

#include "clockwork/serialization/cpp/tests/support/validate_upgradability.hh"

#include "clockwork/repr_iface.hh"
#include "clockwork/serialization/cpp/tachyon_model.hh"
#include "clockwork/serialization/metadata/tachyon_model.pb.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace clockwork::serialization
{

template <typename SrcType, typename DestType>
void validate_upgradability(UpgradeValidationOption validation_option)
{
  metadata::LoggedChannelMetadata src_metadata;
  metadata::TachyonMetadata src_proto;
  REQUIRE(src_proto.ParseFromString(
    std::string{LoggingTraits<SrcType>::schema_definition.data(), LoggingTraits<SrcType>::schema_definition.size()}));
  (*src_metadata.mutable_channel_metadata())["TEST_CHANNEL"].CopyFrom(src_proto);
  metadata::LoggedChannelMetadata dest_metadata;
  metadata::TachyonMetadata dest_proto;
  REQUIRE(dest_proto.ParseFromString(
    std::string{LoggingTraits<DestType>::schema_definition.data(), LoggingTraits<DestType>::schema_definition.size()}));
  dest_proto.set_python_required(false);
  (*dest_metadata.mutable_channel_metadata())["TEST_CHANNEL"].CopyFrom(dest_proto);
  REQUIRE(validate_logged_channel_metadata(src_metadata, dest_metadata));
  switch (validation_option)
  {
  case UpgradeValidationOption::upgrade_only:
    REQUIRE_FALSE(validate_logged_channel_metadata(dest_metadata, src_metadata));
    break;
  case UpgradeValidationOption::upgrade_and_downgrade:
    REQUIRE(validate_logged_channel_metadata(dest_metadata, src_metadata));
    break;
  }
}

template <typename SrcType, typename DestType>
void validate_no_upgradability()
{
  metadata::LoggedChannelMetadata src_metadata;
  metadata::TachyonMetadata src_proto;
  REQUIRE(src_proto.ParseFromString(
    std::string{LoggingTraits<SrcType>::schema_definition.data(), LoggingTraits<SrcType>::schema_definition.size()}));
  (*src_metadata.mutable_channel_metadata())["TEST_CHANNEL"].CopyFrom(src_proto);
  metadata::LoggedChannelMetadata dest_metadata;
  metadata::TachyonMetadata dest_proto;
  REQUIRE(dest_proto.ParseFromString(
    std::string{LoggingTraits<DestType>::schema_definition.data(), LoggingTraits<DestType>::schema_definition.size()}));
  dest_proto.set_python_required(false);
  (*dest_metadata.mutable_channel_metadata())["TEST_CHANNEL"].CopyFrom(dest_proto);
  REQUIRE_FALSE(validate_logged_channel_metadata(src_metadata, dest_metadata));
}

} // namespace clockwork::serialization
