// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/pinion/io_connection.hh"

#include "jewels/std/expected.hh"

#include <memory>

namespace clockwork::pinion
{

jewels::expected<jewels::memory::NonNullSharedPtr<pinion::Observer>, IoConnection::Error>
IoConnection::connect_subscriber(pinion::SubscriberHandle /*subscriber*/)
{
  return jewels::unexpected{Error::unsupported};
}

jewels::expected<void, IoConnection::Error> IoConnection::connect_publisher(pinion::PublisherHandle /*publisher*/)
{
  return jewels::unexpected{Error::unsupported};
}

jewels::expected<void, IoConnection::Error> IoConnection::connect_diagnostics(
  jewels::Uuid<common::EndpointInstanceId> /*endpoint*/, pinion::PublisherHandle /*publisher*/)
{
  return jewels::unexpected{Error::unsupported};
}

} // namespace clockwork::pinion
