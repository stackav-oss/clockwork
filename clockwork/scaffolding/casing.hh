// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/cog/cog_configs.hh"
#include "clockwork/cog/cog_state.hh"
#include "clockwork/cog/factory.hh"
#include "clockwork/cog/interface.hh"
#include "clockwork/common/abstract_cog.hh"
#include "clockwork/common/abstract_epoll_manager.hh"
#include "clockwork/common/abstract_timer.hh"
#include "clockwork/common/forward.hh"
#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/pinion/io_connection.hh"
#include "clockwork/pinion/observer.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/scaffolding/abstract_casing.hh"
#include "clockwork/tags.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"
#include "jewels/uuid/uuid_hasher.hh"

#include <cstddef>
#include <future>
#include <memory>
#include <optional>
#include <span>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace clockwork::scaffolding
{
/// This is a wrapper for C++ types that will be used as schemas, e.g. for cog states
/// It allows the C++ type to be associated with a RepresentationTag is so it can be handled as if it were a normal
/// schema through certain parts of the code.
template <typename TypeT, jewels::Uuid<RepresentationTag> uuid_v>
struct CxxSchema
{
  // NOLINTNEXTLINE(fuchsia-statically-constructed-objects) UUID same for all instances of a type.
  static constexpr jewels::Uuid<RepresentationTag> uuid = uuid_v;
  using Type = TypeT;
};

/// This is a wrapper for Protobuf config types.  Because they don't have any linkage to the clockwork schema they
/// are unusable in isolation.  This wrapper provides a way of associating the protobuf type with a RepresentationTag
/// and the Tap<Tachyon<>> type that will be used by the cog.
/// @{
template <typename ProtoT, jewels::Uuid<RepresentationTag> uuid_v, typename TappyT>
struct ProtoSchema
{
};
/// @}

namespace detail
{
/// Helper for using a ClassFactoryRegistry to make something.  It's here to use the Casing::Error.
template <typename FactoryType, typename... Args>
jewels::expected<typename FactoryType::Ptr, AbstractCasing::Error>
factory_make(const typename FactoryType::IdType& class_id, Args&&... args);

/// Trait to determine if `Cog::set_handle(jewels::Uuid<common::EndpointClassId>, Args...)` exists
/// @{
template <typename Cog, typename Args, typename = void>
struct CogSetHandleCallableImpl : std::false_type
{
};
template <typename Cog, typename... Args>
struct CogSetHandleCallableImpl<
  Cog,
  std::tuple<Args...>,
  std::void_t<decltype(std::declval<Cog>().set_handle(
    jewels::Uuid<common::EndpointClassId>{}, std::declval<Args>()...))>> : std::true_type
{
};
template <typename Cog, typename... Args>
constexpr bool is_cog_set_handle_callable = CogSetHandleCallableImpl<Cog, std::tuple<Args...>>::value;
/// @}

/// Trait to determine if `Thing::create(Args...)` exists
/// @{
template <typename Thing, typename Args, typename = void>
struct CreateCallableImpl : std::false_type
{
};
template <typename Thing, typename... Args>
struct CreateCallableImpl<Thing, std::tuple<Args...>, std::void_t<decltype(Thing::create(std::declval<Args>()...))>>
  : std::true_type
{
};
template <typename Cog, typename... Args>
constexpr bool is_create_callable = CreateCallableImpl<Cog, std::tuple<Args...>>::value;
/// @}

/// Tool for resolving schema operations for either tachyon or protobuf representations
struct Config
{
  template <typename Msg>
  using ValueT = std::shared_ptr<CogConfigDataImpl<Msg>>;
  using IdTag = common::ConfigInstanceId;
  template <typename MsgT>
  struct Traits
  {
    using Msg = void;
  };
  template <typename SchemaT>
  struct Traits<Tap<Tachyon<SchemaT>>>
  {
    using Msg = Tap<Tachyon<SchemaT>>;
    static constexpr auto uuid = Tachyon<SchemaT>::_clockwork_uuid;
    static ValueT<Msg> create(jewels::memory::MemoryResource memres, std::span<const std::byte> data);
  };
  template <typename ProtoT, jewels::Uuid<RepresentationTag> uuid_v, typename SchemaT>
  struct Traits<ProtoSchema<ProtoT, uuid_v, Tap<Tachyon<SchemaT>>>>
  {
    using Msg = Tap<Tachyon<SchemaT>>;
    using Proto = ProtoT;
    // NOLINTNEXTLINE(fuchsia-statically-constructed-objects)  UUID same for all instances of a type.
    static constexpr auto uuid = uuid_v;
    static constexpr bool is_proto_schema = true;
    static ValueT<Msg> create(jewels::memory::MemoryResource memres, std::span<const std::byte> data);
  };
};

/// Tool for resolving schema operations for state objects
struct State
{
  template <typename Msg>
  using ValueT = std::shared_ptr<CogStateDataImpl<Msg>>;
  using IdTag = common::StateInstanceId;
  template <typename MsgT>
  struct Traits
  {
    using Msg = void;
  };
  template <typename SchemaT>
  struct Traits<Tap<Tachyon<SchemaT>>>
  {
    using Msg = Tap<Tachyon<SchemaT>>;
    static constexpr auto uuid = Tachyon<SchemaT>::_clockwork_uuid;
    static ValueT<Msg> create(jewels::memory::MemoryResource memres_sys, pinion::PublisherHandle publisher);
  };
  template <typename CxxType, jewels::Uuid<RepresentationTag> uuid_v>
  struct Traits<CxxSchema<CxxType, uuid_v>>
  {
    using Msg = CxxType;
    static constexpr auto uuid = CxxSchema<CxxType, uuid_v>::uuid;
    static ValueT<Msg> create(jewels::memory::MemoryResource memres_sys, jewels::memory::MemoryResource resource);
  };
};

// Tag type + traits for cogs
struct Cog
{
  template <typename CogT>
  struct Traits
  {
    static constexpr auto uuid = CogT::type_id;
  };
};

/// Helper for getting the message
template <typename Thing, typename Msg>
using ThingTraitMsg = typename Thing::template Traits<Msg>;

/// Tool for comparing Type::uuid, if that field is present via Thing::Traits<Type>
/// @{
template <typename TraitsA, typename TraitsB, typename = void>
struct IsThingUuidEqualImpl : std::false_type
{
};
template <typename TraitsA, typename TraitsB>
struct IsThingUuidEqualImpl<TraitsA, TraitsB, std::void_t<decltype(TraitsA::uuid), decltype(TraitsB::uuid)>>
  : std::bool_constant<TraitsA::uuid == TraitsB::uuid>
{
};
template <typename Thing, typename TypeA, typename TypeB, typename = void>
constexpr bool is_thing_uuid_equal =
  IsThingUuidEqualImpl<typename Thing::template Traits<TypeA>, typename Thing::template Traits<TypeB>>::value;
/// @}

/// Tool for checking if a schema uuid is duplicated
/// @{
template <typename Thing, typename... Types>
struct ThingDedupe
{
};
template <typename Thing, typename Type0, typename... Types>
struct ThingDedupe<Thing, Type0, Types...> : ThingDedupe<Thing, Types...>
{
  static_assert(((!is_thing_uuid_equal<Thing, Type0, Types>) && ...), "Duplicate thing");
};
/// @}

} // namespace detail

///
/// Nominal implementation of AbstractCasing
/// @tparam Cogs a tuple of the cog types this casing should provide
/// @tparam Schemas a tuple of the schema types that this casing should provide for state or config
///
/// @{
template <typename Cogs, typename Schemas, typename IoConnections>
struct CasingImpl;

template <typename... Cogs, typename... Schemas, typename... IoConnections>
struct CasingImpl<std::tuple<Cogs...>, std::tuple<Schemas...>, std::tuple<IoConnections...>> : public AbstractCasing
{
  ///
  /// Construct the casing
  ///
  explicit CasingImpl(jewels::memory::MemoryResource memory_resource);

  ///
  /// Instantiate a Cog.  Instance IDs come from the provided description.
  /// @param description The instance description
  /// @param runner_queue The cog queue for submitting Cogs to the runner
  /// @param execution_resource Resource for per-execution temporary memory
  /// @return A pointer to AbstractCog (to register with runner), or error
  ///
  jewels::expected<std::shared_ptr<AbstractCog>, Error> try_instantiate_cog(
    const Tappy<common::CogInstanceDescription<>>& description,
    std::shared_ptr<AbstractCogQueue> runner_queue,
    jewels::memory::MemoryResource execution_resource) override;

  ///
  /// Instantiate pure serialized state.
  /// Allocates an instance of the given ClassId and assigns it the given InstanceId
  ///
  jewels::expected<void, Error> try_instantiate_state(
    jewels::Uuid<common::StateInstanceId> instance_id,
    jewels::Uuid<RepresentationTag> repr_id,
    pinion::PublisherHandle publisher) override;

  ///
  /// Instantiate pure serialized state with initial data.
  /// Allocates an instance of the given ClassId and assigns it the given InstanceId, then loads data into it
  ///
  Outcome try_instantiate_state(
    jewels::Uuid<common::StateInstanceId> instance_id,
    jewels::Uuid<RepresentationTag> repr_id,
    pinion::PublisherHandle publisher,
    std::span<const std::byte> data) final;

  ///
  /// Instantiate pure C++ state.
  /// Allocates an instance of the given ClassId and assigns it the given InstanceId
  ///
  jewels::expected<void, Error> try_instantiate_state(
    jewels::Uuid<common::StateInstanceId> instance_id,
    jewels::Uuid<RepresentationTag> repr_id,
    jewels::memory::MemoryResource memres) override;

  ///
  /// Instantiate hybrid C++/serialized state.
  /// Allocates an instance of the given ClassId and assigns it the given InstanceId
  ///
  jewels::expected<void, Error> try_instantiate_state(
    jewels::Uuid<common::StateInstanceId> /*unused*/,
    jewels::Uuid<RepresentationTag> /*unused*/,
    pinion::PublisherHandle /*unused*/,
    jewels::memory::MemoryResource /*unused*/) override;

  Outcome try_instantiate_state_from_snapshot(
    jewels::Uuid<common::StateInstanceId> instance_id,
    jewels::Uuid<RepresentationTag> repr_id,
    jewels::Uuid<RepresentationTag> snapshot_repr_id,
    jewels::memory::MemoryResource memres,
    std::span<const std::byte> snapshot_data) override;

  ///
  /// Instantiate Config.
  /// Allocates an instance of the given ClassId and assigns it the given InstanceId
  ///
  jewels::expected<void, Error> try_instantiate_config(
    jewels::Uuid<common::ConfigInstanceId> instance_id,
    jewels::Uuid<RepresentationTag> repr_id,
    std::span<const std::byte> data,
    jewels::memory::MemoryResource memres) override;

  ///
  /// Deserialize data from protobuf format to tachyon format.
  /// @param repr_id The representation ID identifying the schema type
  /// @param input_data The input data in protobuf text format
  /// @param output_data The output buffer to store the deserialized tachyon data
  /// @return Outcome indicating success or specific failure reason
  ///
  Outcome try_deserialize_data(
    jewels::Uuid<RepresentationTag> repr_id,
    std::span<const std::byte> input_data,
    std::span<std::byte> output_data) override;

  ///
  /// Instantiate a local publisher.
  ///
  jewels::expected<void, Error>
  try_connect_publisher(jewels::Uuid<common::EndpointInstanceId> endpoint, pinion::PublisherHandle handle) override;

  ///
  /// Instantiate a local subscriber.
  /// @return A shared pointer to the Observer for that subscriber, which should
  ///   be registered with epoll or a local publisher, or an error.
  ///
  jewels::expected<std::shared_ptr<pinion::Observer>, Error> try_connect_subscriber(
    jewels::Uuid<common::EndpointInstanceId> endpoint, std::shared_ptr<pinion::AbstractChannel> channel) override;

  /// Set a publisher handle for a specific endpoint implementation
  ///
  /// Implementation of AbstractCasing::set_publisher_handle for the concrete casing.
  /// Associates the given publisher handle with the specified endpoint, allowing
  /// non-connected endpoints to have valid handles for optional outputs.
  ///
  /// @param[in] endpoint UUID of the endpoint instance to set the handle for
  /// @param[in] handle Publisher handle to associate with the endpoint
  /// @return Success if handle was set successfully, error code otherwise
  jewels::expected<void, Error>
  set_publisher_handle(jewels::Uuid<common::EndpointInstanceId> endpoint, pinion::PublisherHandle handle) override;

  /// Set a subscriber handle for a specific endpoint implementation
  ///
  /// Implementation of AbstractCasing::set_subscriber for the concrete casing.
  /// Associates the given subscriber handle with the specified endpoint, allowing
  /// non-connected endpoints to have valid handles for optional inputs.
  ///
  /// @param[in] endpoint UUID of the endpoint instance to set the handle for
  /// @param[in] handle Subscriber handle to associate with the endpoint
  /// @return Success if handle was set successfully, error code otherwise
  jewels::expected<void, Error> set_subscriber(jewels::Uuid<common::EndpointInstanceId> endpoint) override;

  ///
  /// Connect a state instance to a Cog instance's state endpoint
  /// @param is_shared: True IFF this state is connected to more than one
  ///   endpoint.  This informs the Cog that it must perform locking of the state
  ///   appropriately.
  ///
  jewels::expected<void, Error> try_connect_state(
    jewels::Uuid<common::EndpointInstanceId> endpoint,
    jewels::Uuid<common::StateInstanceId> state_id,
    bool is_shared) override;

  ///
  /// Connect a Config instance to a Cog instance's config endpoint
  ///
  jewels::expected<void, Error> try_connect_config(
    jewels::Uuid<common::EndpointInstanceId> endpoint, jewels::Uuid<common::ConfigInstanceId> config_id) override;

  ///
  /// Connect a Timer instance to a Cog instance's timer endpoint
  ///
  jewels::expected<std::shared_ptr<pinion::Observer>, Error>
  try_connect_timer(jewels::Uuid<common::EndpointInstanceId> endpoint, std::shared_ptr<AbstractTimer> timer) override;

  ///
  /// Connect a MemoryResource instance to a Cog instance's memres endpoint
  ///
  jewels::expected<void, AbstractCasing::Error> try_connect_memory_resource(
    jewels::Uuid<common::EndpointInstanceId> endpoint, jewels::memory::MemoryResource memres) override;

  ///
  /// Connect a snapshot publisher to a state or config endpoint
  ///
  SnapshotConfigOutcome try_configure_snapshot(const Tappy<common::SnapshotConfig>& snapshot_config) override;

  ///
  /// Try to instantiate an IO stream.
  ///
  jewels::expected<std::shared_ptr<EPollable>, AbstractCasing::Error> try_instantiate_io_connection(
    jewels::Uuid<common::IoConnectionClassId> io_connection,
    jewels::Uuid<common::IoConnectionInstanceId> instance_id,
    std::span<const Tappy<common::EndpointInstanceDescription>> endpoints,
    std::optional<jewels::Uuid<common::EndpointInstanceId>> diags_endpoint_id) override;

  ///
  /// Side-effect-free check that the given representation UUID is in the
  /// `Schemas...` tuple.  See `AbstractCasing::has_schema_representation`.
  ///
  [[nodiscard]] bool has_schema_representation(const jewels::Uuid<RepresentationTag>& repr_id) const noexcept override;

  ///
  /// Side-effect-free check that the given IO connection class UUID is in the
  /// `IoConnections...` tuple.  See `AbstractCasing::has_io_connection_class`.
  ///
  [[nodiscard]] bool
  has_io_connection_class(const jewels::Uuid<common::IoConnectionClassId>& io_id) const noexcept override;

  ///
  /// This is called after all connections are made and before execution begins (including execute_init_cog).  The
  /// casing will validate that all required connections were made and perform any last initialization steps that might
  /// be required before execution.
  ///
  [[nodiscard]] jewels::expected<void, jewels::MonoError> finalize() override;

  ///
  /// This is called after execution (or earlier on error) to communicate to the casing that the scaffolding is cleaning
  /// up.  The casing instance should destruct any held references (e.g. cog instance maps) that may reference
  /// scaffolding state since those objects will be cleaned up prior to the destruction of the casing.
  ///
  void shutdown() override;

  ///
  /// Only execute init after all state/config connections are made.
  /// Execute in the order given in the process description.
  /// No instantiation or connection may be performed after we execute init.
  ///
  jewels::expected<void, Error> execute_init_cog(jewels::Uuid<common::CogInstanceId> /*unused*/) override;

  ///
  /// Start a particular Cog.
  ///
  jewels::expected<void, Error> start_cog(jewels::Uuid<common::CogInstanceId> /*unused*/) override;

  ///
  /// Asynchronously stop a Cog.
  /// @return a future which will be signaled when the Cog is stopped.
  ///
  jewels::expected<std::future<void>, Error> stop_cog(jewels::Uuid<common::CogInstanceId> /*unused*/) override;

private:
  template <typename Tag, typename Type>
  using UuidMap = std::pmr::unordered_map<jewels::Uuid<Tag>, Type, jewels::UuidHasher<Tag>>;

  ///
  /// Internal implementation of try_instantiate_io_connection.  Loops
  /// through the IoConnections... and checks if the uuid matches the
  /// config.  If it finds a match, it instantiates the instance,
  /// registers it internally, and returns the instance.
  ///
  template <class IoConnection0, class... IoConnectionsN>
  jewels::expected<std::shared_ptr<EPollable>, AbstractCasing::Error> try_instantiate_io_connection(
    jewels::Uuid<common::IoConnectionClassId> io_connection,
    jewels::Uuid<common::IoConnectionInstanceId> instance_id,
    std::span<const Tappy<common::EndpointInstanceDescription>> endpoints,
    std::optional<jewels::Uuid<common::EndpointInstanceId>> diags_endpoint_id);

  ///
  /// Generic tool for instancing a Thing (e.g. detail::Config) with the given type_id as the given instance_id using
  /// the provided arguments, which are forwarded to Thing::create()
  ///
  template <typename Thing, typename... Types, typename InstanceId, typename TypeId, typename... Args>
  jewels::expected<void, AbstractCasing::Error>
  try_instantiate_thing(InstanceId instance_id, TypeId type_id, Args&&... args);

  ///
  /// Helper to deserialize a specific schema type from protobuf to tachyon
  /// @tparam SchemaT The schema type to deserialize
  /// @param input_data The input protobuf data
  /// @param output_data The output buffer for tachyon data
  ///
  template <typename SchemaT>
  Outcome try_deserialize_schema(std::span<const std::byte> input_data, std::span<std::byte> output_data);

  ///
  /// Dispatches to a cog instance's set_handle.  If Cog0 defines a matching `set_handle(endpoint, args...)` and an
  /// instance of Cog0 defines the matching EndpointInstanceId then it calls set_handle with the provided args and
  /// forwards its return value (if any).  If either condition is false, recurses to the next Cog in CogN.
  ///
  template <typename RetT, typename... Args>
  [[nodiscard]] jewels::expected<RetT, Error>
  set_handle(jewels::Uuid<common::EndpointInstanceId> endpoint, Args&&... args);

  ///
  /// Memory resource used for internal collections
  ///
  jewels::memory::MemoryResource memres_;

  ///
  /// Factories
  ///
  UuidMap<common::CogClassId, std::shared_ptr<CogFactory>> cog_factories_;

  ///
  /// Cog instances by type and uuid
  ///
  UuidMap<common::CogInstanceId, std::shared_ptr<CogBase>> cogs_;

  ///
  /// IO Connections uuid
  ///
  UuidMap<
    common::EndpointInstanceId,
    std::pair<jewels::memory::NonNullSharedPtr<pinion::IoConnection>, jewels::Uuid<common::EndpointClassId>>>
    io_connections_;
  UuidMap<common::EndpointInstanceId, jewels::memory::NonNullSharedPtr<pinion::IoConnection>> io_connection_diags_;

  ///
  /// Endpoints (cog + class uuid)
  ///
  UuidMap<common::EndpointInstanceId, std::pair<std::shared_ptr<CogBase>, jewels::Uuid<common::EndpointClassId>>>
    endpoints_;

  ///
  /// Config/state instances
  ///
  UuidMap<common::ConfigInstanceId, std::shared_ptr<CogConfigData>> configs_;
  UuidMap<common::StateInstanceId, std::shared_ptr<CogStateData>> states_;
};
/// @}

} // namespace clockwork::scaffolding

#include "clockwork/scaffolding/casing.inl"
