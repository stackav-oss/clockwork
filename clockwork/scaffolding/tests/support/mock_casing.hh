// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/scaffolding/abstract_casing.hh"

#include <trompeloeil.hpp>

#include <algorithm>
#include <span>

namespace std
{
/// Comparison operator needed by trampoloeil
/// @param[in] lhs Left hand operand
/// @param[in] rhs Right hand operand
/// @return True iff contents of lhs and rhs match
template <class T>
// NOLINTNEXTLINE(cert-dcl58-cpp): Modifying std for test purposes
constexpr bool operator==(const std::span<T> lhs, const std::span<T> rhs)
{
  return std::ranges::equal(lhs, rhs);
}
} // namespace std

namespace clockwork::scaffolding
{

class MockCasing : public AbstractCasing
{
public:
  MockCasing() = default;

  MAKE_MOCK3(
    try_instantiate_cog,
    (jewels::expected<std::shared_ptr<AbstractCog>, Error>)(const common::CogInstanceDescriptionTap&,
                                                            std::shared_ptr<AbstractCogQueue>,
                                                            jewels::memory::MemoryResource),
    override);

  MAKE_MOCK3(
    try_instantiate_state,
    (jewels::expected<void, Error>)(jewels::Uuid<common::StateInstanceId>,
                                    jewels::Uuid<RepresentationTag>,
                                    pinion::PublisherHandle),
    override);

  MAKE_MOCK3(
    try_instantiate_state,
    (jewels::expected<void, Error>)(jewels::Uuid<common::StateInstanceId>,
                                    jewels::Uuid<RepresentationTag>,
                                    jewels::memory::MemoryResource),
    override);

  MAKE_MOCK4(
    try_instantiate_state,
    (jewels::expected<void, Error>)(jewels::Uuid<common::StateInstanceId>,
                                    jewels::Uuid<RepresentationTag>,
                                    pinion::PublisherHandle,
                                    jewels::memory::MemoryResource),
    override);

  MAKE_MOCK4(
    try_instantiate_config,
    (jewels::expected<void, Error>)(jewels::Uuid<common::ConfigInstanceId>,
                                    jewels::Uuid<RepresentationTag>,
                                    std::span<const std::byte>,
                                    jewels::memory::MemoryResource),
    override);

  MAKE_MOCK2(
    try_connect_publisher,
    (jewels::expected<void, Error>)(jewels::Uuid<common::EndpointInstanceId>, pinion::PublisherHandle),
    override);

  MAKE_MOCK2(
    try_connect_subscriber,
    (jewels::expected<std::shared_ptr<pinion::Observer>, Error>)(jewels::Uuid<common::EndpointInstanceId>,
                                                                 pinion::SubscriberHandle),
    override);

  MAKE_MOCK2(
    set_publisher_handle,
    (jewels::expected<void, Error>)(jewels::Uuid<common::EndpointInstanceId>, pinion::PublisherHandle),
    override);

  MAKE_MOCK1(set_subscriber, (jewels::expected<void, Error>)(jewels::Uuid<common::EndpointInstanceId>), override);

  MAKE_MOCK3(
    try_connect_state,
    (jewels::expected<void, Error>)(jewels::Uuid<common::EndpointInstanceId>,
                                    jewels::Uuid<common::StateInstanceId>,
                                    bool is_shared),
    override);

  MAKE_MOCK2(
    try_connect_config,
    (jewels::expected<void, Error>)(jewels::Uuid<common::EndpointInstanceId>, jewels::Uuid<common::ConfigInstanceId>),
    override);

  MAKE_MOCK2(
    try_connect_timer,
    (jewels::expected<std::shared_ptr<pinion::Observer>, Error>)(jewels::Uuid<common::EndpointInstanceId>,
                                                                 std::shared_ptr<AbstractTimer>),
    override);

  MAKE_MOCK2(
    try_connect_memory_resource,
    (jewels::expected<void, Error>)(jewels::Uuid<common::EndpointInstanceId>, jewels::memory::MemoryResource),
    override);

  MAKE_MOCK4(
    try_instantiate_io_connection,
    (jewels::expected<std::shared_ptr<EPollable>, Error>)(jewels::Uuid<common::IoConnectionClassId>,
                                                          jewels::Uuid<common::IoConnectionInstanceId>,
                                                          std::span<const Tappy<common::EndpointInstanceDescription>>,
                                                          std::optional<jewels::Uuid<common::EndpointInstanceId>>),
    override);

  MAKE_MOCK0(finalize, (jewels::expected<void, jewels::MonoError>)(), override);

  MAKE_MOCK0(shutdown, (void)(), override);

  MAKE_MOCK1(execute_init_cog, (jewels::expected<void, Error>)(jewels::Uuid<common::CogInstanceId>), override);

  MAKE_MOCK1(start_cog, (jewels::expected<void, Error>)(jewels::Uuid<common::CogInstanceId>), override);

  MAKE_MOCK1(stop_cog, (jewels::expected<std::future<void>, Error>)(jewels::Uuid<common::CogInstanceId>), override);
};

} // namespace clockwork::scaffolding
