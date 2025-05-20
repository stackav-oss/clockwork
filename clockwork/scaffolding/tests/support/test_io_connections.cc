// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/scaffolding/tests/support/test_io_connections.hh"

#include "clockwork/pinion/io_connection.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "clockwork/pinion/tests/support/pub_sub.hh"
#include "jewels/memory/pmr_shared_ptr.hh"

#include <memory>

namespace clockwork::testing
{

jewels::expected<void, pinion::IoConnection::Error>
TestIoConnection::connect_publisher(pinion::PublisherHandle /*publisher*/)
{
  if (publisher_set)
  {
    return jewels::unexpected{pinion::IoConnection::Error::already_connected};
  }

  publisher_set = true;
  return {};
}

jewels::expected<void, pinion::IoConnection::Error> TestIoConnection::connect_diagnostics(
  jewels::Uuid<common::EndpointInstanceId> /*endpoint*/, pinion::PublisherHandle /*publisher*/)
{
  if (diags_set)
  {
    return jewels::unexpected{pinion::IoConnection::Error::already_connected};
  }

  diags_set = true;
  return {};
}

jewels::expected<jewels::memory::NonNullSharedPtr<pinion::Observer>, pinion::IoConnection::Error>
TestIoConnection::connect_subscriber(pinion::SubscriberHandle /*subscriber*/)
{
  if (subscriber_set)
  {
    return jewels::unexpected{pinion::IoConnection::Error::already_connected};
  }

  subscriber_set = true;
  return {jewels::memory::NonNullSharedPtr<pinion::Observer>{std::make_shared<FakeObserver>()}};
}

// Needed to avoid changing the API just for a unit test.
bool TestIoConnection::publisher_set{}; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

// Needed to avoid changing the API just for a unit test.
bool TestIoConnection::subscriber_set{}; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

// Needed to avoid changing the API just for a unit test.
bool TestIoConnection::diags_set{}; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

jewels::expected<jewels::memory::NonNullSharedPtr<TestIoConnection>, TestIoConnectionError>
TestIoConnection::try_make(jewels::memory::MemoryResource memres)
{
  return jewels::memory::NonNullSharedPtr<TestIoConnection>{jewels::memory::make_pmr_shared<TestIoConnection>(memres)};
}

void TestIoConnection::reset()
{
  publisher_set = false;
  subscriber_set = false;
  diags_set = false;
}

jewels::expected<jewels::memory::NonNullSharedPtr<pinion::IoConnection>, TestIoConnectionError>
TestFailingIoConnection::try_make(jewels::memory::MemoryResource /*memres*/)
{
  return jewels::unexpected{TestIoConnectionError::failed_to_create};
}

jewels::expected<jewels::memory::NonNullSharedPtr<TestEPollableIoConnection>, TestIoConnectionError>
TestEPollableIoConnection::try_make(jewels::memory::MemoryResource memres)
{
  return jewels::memory::NonNullSharedPtr<TestEPollableIoConnection>{
    jewels::memory::make_pmr_shared<TestEPollableIoConnection>(memres)};
}

jewels::expected<void, jewels::MonoError> TestEPollableIoConnection::register_with(AbstractEPollManager& /*manager*/)
{
  // Always succeed.
  return {};
}

} // namespace clockwork::testing
