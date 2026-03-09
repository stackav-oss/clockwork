// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/common/abstract_epoll_manager.hh"
#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/pinion/io_connection.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"

#include <gsl/util>
#include <wise_enum.h>

#include <cstdint>

namespace clockwork::testing
{

WISE_ENUM_CLASS((TestIoConnectionError, uint8_t), failed_to_create);

struct TestIoConnection : public pinion::IoConnection
{
  /// IO connection uuid.
  static constexpr auto uuid =
    *jewels::Uuid<common::IoConnectionClassId>::from_string("eeeeeeee-eeee-eeee-4312-000000000000");

  /// Make an IO connection that always succeeds to connect.
  static jewels::expected<jewels::memory::NonNullSharedPtr<TestIoConnection>, TestIoConnectionError>
  try_make(jewels::memory::MemoryResource memres);

  static void reset();

  /// Connect publisher.  Succeeds the first time.
  jewels::expected<void, pinion::IoConnection::Error> connect_publisher(
    jewels::Uuid<common::EndpointClassId> /*endpoint_id*/, pinion::PublisherHandle /*publisher*/) final;

  /// Connect diags.  Succeeds the first time.
  jewels::expected<void, pinion::IoConnection::Error> connect_diagnostics(
    jewels::Uuid<common::EndpointInstanceId> /*endpoint*/, pinion::PublisherHandle /*publisher*/) final;

  /// Connect subscriber.  Succeeds the first time.
  jewels::expected<jewels::memory::NonNullSharedPtr<pinion::Observer>, pinion::IoConnection::Error> connect_subscriber(
    jewels::Uuid<common::EndpointClassId> /*endpoint_id*/, pinion::SubscriberHandle /*subscriber*/) final;

  /// Whether or not the publisher is set.
  /// Needed to avoid changing the API just for a unit test.
  static bool
    publisher_set; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables, readability-identifier-naming)

  /// Whether or not the subscriber is set.
  /// Needed to avoid changing the API just for a unit test.
  static bool
    subscriber_set; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables, readability-identifier-naming)

  /// Whether or not the diags publisher is set.
  /// Needed to avoid changing the API just for a unit test.
  static bool diags_set; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables, readability-identifier-naming)
};

struct TestEPollableIoConnection : public pinion::IoConnection, public EPollable
{
  /// IO connection uuid.
  static constexpr auto uuid =
    *jewels::Uuid<common::IoConnectionClassId>::from_string("eeeeeeee-eeee-eeee-4312-000000000002");

  /// Make an IO connection that is an EPollable.
  static jewels::expected<jewels::memory::NonNullSharedPtr<TestEPollableIoConnection>, TestIoConnectionError>
  try_make(jewels::memory::MemoryResource memres);

  /// Implements the pure virtual method with zero side effects.
  jewels::expected<void, jewels::MonoError> register_with(AbstractEPollManager& manager) final;
};

struct TestFailingIoConnection
{
  /// IO connection uuid.
  static constexpr auto uuid =
    *jewels::Uuid<common::IoConnectionClassId>::from_string("eeeeeeee-eeee-eeee-4312-000000000001");

  /// Make an IO connection that always fails to connect.
  static jewels::expected<jewels::memory::NonNullSharedPtr<pinion::IoConnection>, TestIoConnectionError>
    try_make(jewels::memory::MemoryResource /*memres*/);
};

} // namespace clockwork::testing
