// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/common/abstract_cog.hh"
#include "clockwork/common/abstract_timer.hh"
#include "clockwork/common/forward.hh"
#include "clockwork/common/process_description.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/io_connection.hh"
#include "clockwork/pinion/observer.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "clockwork/tags.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"

#include <wise_enum.h>

#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <span>

namespace clockwork::scaffolding
{

class AbstractCasing
{
public:
  template <typename Tag>
  using Uuid = jewels::Uuid<Tag>;

  static constexpr uint32_t max_endpoints_per_cog{11U};
  static constexpr uint32_t max_instance_path_size{512U};

  WISE_ENUM_CLASS_MEMBER(
    (Error, uint8_t),
    invalid_class_uuid,
    invalid_instance_uuid,
    buffer_error,
    init_failure,
    connect_publisher_failure,
    connect_subscriber_failure,
    empty_endpoint_ids)

  AbstractCasing() = default;
  virtual ~AbstractCasing() = default;

  AbstractCasing(const AbstractCasing&) = delete;
  AbstractCasing(AbstractCasing&&) = delete;
  AbstractCasing& operator=(const AbstractCasing&) = delete;
  AbstractCasing& operator=(AbstractCasing&&) = delete;

  // Instantiate a Cog.  Instance IDs come from the provided description.
  //
  // @param description The instance description.
  // @param runner_queue The cog queue for submitting Cogs to the runner.
  // @param execution_resource Resource for per-execution temporary memory.
  //   Should match the memory_resource_id from the cog instance description.
  // @return A pointer to AbstractCog (to register with runner), or error.
  virtual jewels::expected<std::shared_ptr<AbstractCog>, Error> try_instantiate_cog(
    const common::CogInstanceDescriptionTap& description,
    std::shared_ptr<AbstractCogQueue> runner_queue,
    jewels::memory::MemoryResource execution_resource) = 0;

  // Instantiate pure serialized state.
  // Allocates an instance of the given ClassId and assigns it the given InstanceId
  virtual jewels::expected<void, Error> try_instantiate_state(
    jewels::Uuid<common::StateInstanceId>, jewels::Uuid<RepresentationTag>, pinion::PublisherHandle) = 0;

  // Instantiate pure C++ state.
  // Allocates an instance of the given ClassId and assigns it the given InstanceId
  virtual jewels::expected<void, Error> try_instantiate_state(
    jewels::Uuid<common::StateInstanceId>, jewels::Uuid<RepresentationTag>, jewels::memory::MemoryResource) = 0;

  // Instantiate hybrid C++/serialized state.
  // Allocates an instance of the given ClassId and assigns it the given InstanceId
  virtual jewels::expected<void, Error> try_instantiate_state(
    jewels::Uuid<common::StateInstanceId>,
    jewels::Uuid<RepresentationTag>,
    pinion::PublisherHandle,
    jewels::memory::MemoryResource) = 0;

  // Instantiate Config.
  // Allocates an instance of the given ClassId and assigns it the given InstanceId
  virtual jewels::expected<void, Error> try_instantiate_config(
    jewels::Uuid<common::ConfigInstanceId>,
    jewels::Uuid<RepresentationTag>,
    std::span<const std::byte> buffer,
    jewels::memory::MemoryResource) = 0;

  // Instantiate a local publisher.
  virtual jewels::expected<void, Error>
    try_connect_publisher(jewels::Uuid<common::EndpointInstanceId>, pinion::PublisherHandle) = 0;

  // Instantiate a local subscriber.
  // @return A shared pointer to the Observer for that subscriber, which should
  //   be registered with epoll or a local publisher, or an error.
  virtual jewels::expected<std::shared_ptr<pinion::Observer>, Error>
    try_connect_subscriber(jewels::Uuid<common::EndpointInstanceId>, pinion::SubscriberHandle) = 0;

  // Connect a state instance to a Cog instance's state endpoint
  // @param is_shared: True IFF this state is connected to more than one
  //   endpoint.  This informs the Cog that it must perform locking of the state
  //   appropriately.
  virtual jewels::expected<void, Error> try_connect_state(
    jewels::Uuid<common::EndpointInstanceId>, jewels::Uuid<common::StateInstanceId>, bool is_shared) = 0;

  // Connect a Config instance to a Cog instance's config endpoint
  virtual jewels::expected<void, Error>
    try_connect_config(jewels::Uuid<common::EndpointInstanceId>, jewels::Uuid<common::ConfigInstanceId>) = 0;

  // Connect a Timer instance to a Cog instance's timer endpoint
  virtual jewels::expected<std::shared_ptr<pinion::Observer>, Error>
    try_connect_timer(jewels::Uuid<common::EndpointInstanceId>, std::shared_ptr<AbstractTimer>) = 0;

  // Connect a MemoryResource instance to a Cog instance's memres endpoint
  virtual jewels::expected<void, Error>
    try_connect_memory_resource(jewels::Uuid<common::EndpointInstanceId>, jewels::memory::MemoryResource) = 0;

  // Instantiate an IO object.
  virtual jewels::expected<std::shared_ptr<EPollable>, Error> try_instantiate_io_connection(
    jewels::Uuid<common::IoConnectionClassId> io_connection,
    jewels::Uuid<common::IoConnectionInstanceId> instance_id,
    std::span<const Tappy<common::EndpointInstanceDescription>> endpoints,
    std::optional<jewels::Uuid<common::EndpointInstanceId>> diags_endpoint_id) = 0;

  // This is called after all connections are made and before execution begins (including execute_init_cog).  The casing
  // will validate that all required connections were made and perform any last initialization steps that might be
  // required before execution.
  [[nodiscard]] virtual jewels::expected<void, jewels::MonoError> finalize() = 0;

  // This is called after execution (or earlier on error) to communicate to the casing that the scaffolding is cleaning
  // up.  The casing instance should destruct any held references (e.g. cog instance maps) that may reference
  // scaffolding state since those objects will be cleaned up prior to the destruction of the casing.
  virtual void shutdown() = 0;

  /// Only execute init after all state/config connections are made.
  /// Execute in the order given in the process description.
  /// No instantiation or connection may be performed after we execute init.
  virtual jewels::expected<void, Error> execute_init_cog(jewels::Uuid<common::CogInstanceId>) = 0;

  // Start a particular Cog.
  virtual jewels::expected<void, Error> start_cog(jewels::Uuid<common::CogInstanceId>) = 0;

  // Asynchronously stop a Cog.
  // @return a future which will be signaled when the Cog is stopped.
  virtual jewels::expected<std::future<void>, Error> stop_cog(jewels::Uuid<common::CogInstanceId>) = 0;
};

/// Instantiate the Casing implementation
///
/// @param memory_resource Allocator for bookkeeping data and any instantiated
///   objects not given their own resource.
std::shared_ptr<AbstractCasing> make_casing(jewels::memory::MemoryResource memory_resource);

} // namespace clockwork::scaffolding
