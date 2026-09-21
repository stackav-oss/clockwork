// IWYU pragma: private, include "clockwork/scaffolding/casing.hh"
#pragma once

#include "clockwork/scaffolding/casing.hh"

#include "clockwork/cog/cog_configs.hh"
#include "clockwork/cog/cog_state.hh"
#include "clockwork/cog/factory.hh"
#include "clockwork/cog/interface.hh"
#include "clockwork/common/abstract_epoll_manager.hh"
#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/pinion/abstract_channel.hh"
#include "clockwork/pinion/io_connection.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/scaffolding/abstract_casing.hh"
#include "clockwork/tags.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/container/compare.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_shared_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"
#include "jewels/uuid/uuid_hasher.hh"

#include <google/protobuf/io/zero_copy_stream_impl_lite.h>
#include <google/protobuf/text_format.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <functional>
#include <future>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <memory_resource>
#include <new>
#include <optional>
#include <span>
#include <tuple>
#include <type_traits>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <variant>

namespace clockwork::scaffolding
{
namespace detail
{
/// Check if an IO Connection pointer type is EPollable.
template <class IoConnectionReturnType>
inline constexpr bool is_epollable_v{false};
/// Specialization for NonNullSharedPtr.
template <class IoConnectionType>
inline constexpr bool is_epollable_v<jewels::memory::NonNullSharedPtr<IoConnectionType>>{
  std::is_base_of_v<EPollable, IoConnectionType>};
} // namespace detail

template <typename FactoryType, typename... Args>
jewels::expected<typename FactoryType::Ptr, AbstractCasing::Error>
detail::factory_make(const typename FactoryType::IdType& class_id, Args&&... args)
{
  if (const auto* factory = FactoryType::find(class_id); factory)
  {
    if (auto ptr = factory->make(std::forward<Args>(args)...); ptr)
    {
      return {std::move(ptr)};
    }
    return jewels::unexpected(AbstractCasing::Error::init_failure);
  }
  return jewels::unexpected(AbstractCasing::Error::invalid_class_uuid);
}

template <typename SchemaT>
auto detail::Config::Traits<Tap<Tachyon<SchemaT>>>::create(
  jewels::memory::MemoryResource memres, std::span<const std::byte> data) -> ValueT<Msg>
{
  if (sizeof(Msg) != data.size())
  {
    jewels::log_cerr_error("config file size mismatch: got {}, expected {}", data.size(), sizeof(Msg));
    return nullptr;
  }
  auto config =
    jewels::memory::make_pmr_shared<CogConfigDataImpl<Msg>>(memres, jewels::memory::make_pmr_shared<Msg>(memres));
  std::memcpy(config->data.get(), data.data(), data.size());
  return config;
}

template <typename ProtoT, jewels::Uuid<RepresentationTag> uuid_v, typename SchemaT>
auto detail::Config::Traits<ProtoSchema<ProtoT, uuid_v, Tap<Tachyon<SchemaT>>>>::create(
  jewels::memory::MemoryResource memres, std::span<const std::byte> data) -> ValueT<Msg>
{
  google::protobuf::io::ArrayInputStream stream{data.data(), static_cast<int>(data.size())};
  ProtoT proto;
  if (!google::protobuf::TextFormat::Parse(&stream, &proto))
  {
    jewels::log_cerr_error("protobuf failed to parse (type={})", typeid(SchemaT).name());
    return nullptr;
  }
  auto config =
    jewels::memory::make_pmr_shared<CogConfigDataImpl<Msg>>(memres, jewels::memory::make_pmr_shared<Msg>(memres));
  if (!protobuf_to_tap(*config->data, proto))
  {
    return nullptr;
  }
  return config;
}

template <typename SchemaT>
auto detail::State::Traits<Tap<Tachyon<SchemaT>>>::create(
  jewels::memory::MemoryResource memres_sys, pinion::PublisherHandle publisher) -> ValueT<Msg>
{
  return jewels::memory::make_pmr_shared<CogStateDataImpl<Msg>>(memres_sys, std::move(publisher));
}

template <typename CxxType, jewels::Uuid<RepresentationTag> uuid_v>
auto detail::State::Traits<CxxSchema<CxxType, uuid_v>>::create(
  jewels::memory::MemoryResource memres_sys, jewels::memory::MemoryResource resource) -> ValueT<Msg>
{
  return jewels::memory::make_pmr_shared<CogStateDataImpl<Msg>>(memres_sys, resource);
}

template <typename... Cogs, typename... Schemas, typename... IoConnections>
CasingImpl<std::tuple<Cogs...>, std::tuple<Schemas...>, std::tuple<IoConnections...>>::CasingImpl(
  jewels::memory::MemoryResource memory_resource)
  : memres_(std::move(memory_resource)),
    cog_factories_({{{}, {}}, {Cogs::type_id, std::make_shared<Cogs>()}...}, memres_),
    cogs_(memres_),
    io_connections_(memres_),
    endpoints_(memres_),
    configs_(memres_),
    states_(memres_)
{
  // Ignoring is okay because this is only used to validate uniqueness
  // of the types.  This will trigger a static_assert if not unique.
  std::ignore = detail::ThingDedupe<detail::Cog, Cogs...>{};
  std::ignore = detail::ThingDedupe<detail::Config, Schemas...>{};
}

template <typename... Cogs, typename... Schemas, typename... IoConnections>
jewels::expected<std::shared_ptr<AbstractCog>, AbstractCasing::Error>
CasingImpl<std::tuple<Cogs...>, std::tuple<Schemas...>, std::tuple<IoConnections...>>::try_instantiate_cog(
  const Tappy<common::CogInstanceDescription<>>& description,
  std::shared_ptr<AbstractCogQueue> runner_queue,
  jewels::memory::MemoryResource execution_resource)
{
  auto cog = detail::factory_make<CogFactory>(
    description.get_cog_class_id(),
    execution_resource,
    description.get_cog_instance_id(),
    jewels::memory::make_non_null_from_ref(*runner_queue));
  if (cog)
  {
    cogs_[description.get_cog_instance_id()] = *cog;
    for (const auto& endpoint : description.get_endpoints())
    {
      endpoints_[endpoint.get_endpoint_instance_id()] = std::pair(*cog, endpoint.get_endpoint_class_id());
    }
  }
  return cog;
}

template <typename... Cogs, typename... Schemas, typename... IoConnections>
jewels::expected<void, AbstractCasing::Error>
CasingImpl<std::tuple<Cogs...>, std::tuple<Schemas...>, std::tuple<IoConnections...>>::try_instantiate_state(
  jewels::Uuid<common::StateInstanceId> instance_id,
  jewels::Uuid<RepresentationTag> repr_id,
  pinion::PublisherHandle publisher)
{
  return detail::factory_make<CogStateFactory>(repr_id, memres_, std::move(publisher))
    .transform([&](auto&& state) { states_[instance_id] = std::forward<decltype(state)>(state); });
}

template <typename... Cogs, typename... Schemas, typename... IoConnections>
auto CasingImpl<std::tuple<Cogs...>, std::tuple<Schemas...>, std::tuple<IoConnections...>>::try_instantiate_state(
  jewels::Uuid<common::StateInstanceId> instance_id,
  jewels::Uuid<RepresentationTag> repr_id,
  pinion::PublisherHandle publisher,
  std::span<const std::byte> data) -> Outcome
{
  auto factory_result = detail::factory_make<CogStateFactory>(repr_id, memres_, std::move(publisher));
  if (!factory_result)
  {
    return static_cast<OutcomeEnum>(factory_result.error());
  }

  auto& state = *factory_result;
  if (jewels::fails(state->set_from_bytes(data)))
  {
    return OutcomeEnum::buffer_error;
  }
  states_[instance_id] = std::move(state);

  return OutcomeEnum::success;
}

template <typename... Cogs, typename... Schemas, typename... IoConnections>
jewels::expected<void, AbstractCasing::Error>
CasingImpl<std::tuple<Cogs...>, std::tuple<Schemas...>, std::tuple<IoConnections...>>::try_instantiate_state(
  jewels::Uuid<common::StateInstanceId> instance_id,
  jewels::Uuid<RepresentationTag> repr_id,
  jewels::memory::MemoryResource memres)
{
  return detail::factory_make<CogStateFactory>(repr_id, memres_, std::move(memres))
    .transform([&](auto&& state) { states_[instance_id] = std::forward<decltype(state)>(state); });
}

template <typename... Cogs, typename... Schemas, typename... IoConnections>
jewels::expected<void, AbstractCasing::Error>
CasingImpl<std::tuple<Cogs...>, std::tuple<Schemas...>, std::tuple<IoConnections...>>::try_instantiate_state(
  jewels::Uuid<common::StateInstanceId> /*unused*/,
  jewels::Uuid<RepresentationTag> /*unused*/,
  pinion::PublisherHandle /*unused*/,
  jewels::memory::MemoryResource /*unused*/)
{
  return jewels::unexpected(AbstractCasing::Error::invalid_instance_uuid);
}

template <typename... Cogs, typename... Schemas, typename... IoConnections>
auto CasingImpl<std::tuple<Cogs...>, std::tuple<Schemas...>, std::tuple<IoConnections...>>::
  try_instantiate_state_from_snapshot(
    jewels::Uuid<common::StateInstanceId> instance_id,
    jewels::Uuid<RepresentationTag> repr_id,
    jewels::Uuid<RepresentationTag> snapshot_repr_id,
    jewels::memory::MemoryResource memres,
    std::span<const std::byte> snapshot_data) -> Outcome
{
  const auto* factory = CogStateFactory::find(repr_id);
  if (factory == nullptr)
  {
    return OutcomeEnum::invalid_class_uuid;
  }

  CogStateFactory::Ptr state;
  const auto result = factory->make(jewels::Out{state}, memres_, std::move(memres), snapshot_repr_id, snapshot_data);
  switch (result.get())
  {
  case CogStateFactory::StateRestoreResult::success:
    if (!state)
    {
      return OutcomeEnum::init_failure;
    }
    states_[instance_id] = std::move(state);
    return OutcomeEnum::success;
  case CogStateFactory::StateRestoreResult::invalid_class_uuid:
    return OutcomeEnum::invalid_class_uuid;
  case CogStateFactory::StateRestoreResult::buffer_error:
    return OutcomeEnum::buffer_error;
  case CogStateFactory::StateRestoreResult::init_failure:
    return OutcomeEnum::init_failure;
  }
  return OutcomeEnum::init_failure;
}

template <typename... Cogs, typename... Schemas, typename... IoConnections>
jewels::expected<void, AbstractCasing::Error>
CasingImpl<std::tuple<Cogs...>, std::tuple<Schemas...>, std::tuple<IoConnections...>>::try_instantiate_config(
  jewels::Uuid<common::ConfigInstanceId> instance_id,
  jewels::Uuid<RepresentationTag> repr_id,
  std::span<const std::byte> data,
  jewels::memory::MemoryResource memres)
{
  return try_instantiate_thing<detail::Config, Schemas...>(instance_id, repr_id, memres, data);
}

template <typename... Cogs, typename... Schemas, typename... IoConnections>
auto CasingImpl<std::tuple<Cogs...>, std::tuple<Schemas...>, std::tuple<IoConnections...>>::try_deserialize_data(
  jewels::Uuid<RepresentationTag> repr_id, std::span<const std::byte> input_data, std::span<std::byte> output_data)
  -> Outcome
{
  Outcome result{OutcomeEnum::invalid_class_uuid};
  auto maybe_deserialize = [this, &result, &repr_id, &input_data, &output_data]<typename SchemaT>() mutable
  {
    // This gets marked as unused if the if-constexpr is false
    (void)this;
    using Traits = detail::Config::template Traits<SchemaT>;
    if constexpr (!std::is_same_v<typename Traits::Msg, void>)
    {
      if (Traits::uuid == repr_id)
      {
        result = try_deserialize_schema<SchemaT>(input_data, output_data);
        return true;
      }
    }
    return false;
  };
  // Errors are returned via the `result` variable
  // The lambda's return is used only for short-circuiting in the fold expression.
  std::ignore = (maybe_deserialize.template operator()<Schemas>() || ...);
  return result;
}

template <typename... Cogs, typename... Schemas, typename... IoConnections>
template <typename SchemaT>
auto CasingImpl<std::tuple<Cogs...>, std::tuple<Schemas...>, std::tuple<IoConnections...>>::try_deserialize_schema(
  std::span<const std::byte> input_data, std::span<std::byte> output_data) -> Outcome
{
  using Traits = detail::Config::template Traits<SchemaT>;
  using MsgType = typename Traits::Msg;

  // Check if output buffer is the right size
  if (sizeof(MsgType) != output_data.size())
  {
    return OutcomeEnum::buffer_error;
  }

  // Only ProtoSchema types can be deserialized from protobuf
  // Detect this using the is_proto_schema trait
  if constexpr (requires { Traits::is_proto_schema; })
  {
    using ProtoT = typename Traits::Proto;

    google::protobuf::io::ArrayInputStream stream{input_data.data(), static_cast<int>(input_data.size())};
    ProtoT proto;
    if (!google::protobuf::TextFormat::Parse(&stream, &proto))
    {
      jewels::log_cerr_error("protobuf failed to parse for deserialization");
      return OutcomeEnum::init_failure;
    }

    // Construct tachyon message in the output buffer using placement new
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)  - Buffer managed by caller
    auto* tachyon_msg = new (output_data.data()) MsgType{};
    if (!protobuf_to_tap(*tachyon_msg, proto))
    {
      return OutcomeEnum::init_failure;
    }

    return OutcomeEnum::success;
  }
  // Not a ProtoSchema type, cannot deserialize
  return OutcomeEnum::invalid_class_uuid;
}

template <typename... Cogs, typename... Schemas, typename... IoConnections>
template <typename Thing, typename... Types, typename InstanceId, typename TypeId, typename... Args>
jewels::expected<void, AbstractCasing::Error>
CasingImpl<std::tuple<Cogs...>, std::tuple<Schemas...>, std::tuple<IoConnections...>>::
  try_instantiate_thing( // NOLINT(readability-function-size)
    InstanceId instance_id,
    TypeId type_id,
    Args&&... args) // NOLINT(cppcoreguidelines-missing-std-forward)
{
  jewels::expected<void, AbstractCasing::Error> result = jewels::unexpected(AbstractCasing::Error::invalid_class_uuid);
  auto maybe_instantiate = [this, &instance_id, &result, &type_id, &args...]<typename Type>() mutable
  {
    using Traits = Thing::template Traits<Type>;
    if constexpr (detail::is_create_callable<Traits, Args...>)
    {
      if (Traits::uuid == type_id)
      {
        auto instance = Traits::create(std::forward<Args>(args)...);
        if (instance)
        {
          if constexpr (std::is_same_v<Thing, detail::State>)
          {
            states_[instance_id] = std::move(instance);
          }
          else if constexpr (std::is_same_v<Thing, detail::Config>)
          {
            configs_[instance_id] = std::move(instance);
          }
          else
          {
            static_assert(!std::is_void_v<Thing>);
          }
          result = {};
        }
        else
        {
          result = jewels::unexpected(AbstractCasing::Error::init_failure);
        }
        return true;
      }
    }
    return false;
  };
  // Errors are returned via the `result` variable
  // The lambda's return is used only for short-circuiting in the fold expression.
  std::ignore = (maybe_instantiate.template operator()<Types>() || ...);
  return result;
}

template <typename... Cogs, typename... Schemas, typename... IoConnections>
jewels::expected<void, AbstractCasing::Error>
CasingImpl<std::tuple<Cogs...>, std::tuple<Schemas...>, std::tuple<IoConnections...>>::try_connect_publisher(
  jewels::Uuid<common::EndpointInstanceId> endpoint, pinion::PublisherHandle handle)
{
  if (auto iter = io_connections_.find(endpoint); iter != std::end(io_connections_))
  {
    auto& [io_connection, endpoint_class_id] = iter->second;
    if (auto result = io_connection->connect_publisher(endpoint_class_id, std::move(handle)); !result)
    {
      jewels::log_cerr_error(
        "Failed to connect publisher to IO connection {} with error: {}", endpoint, result.error());
      return jewels::unexpected{AbstractCasing::Error::connect_publisher_failure};
    }
    return {};
  }
  if (auto iter = io_connection_diags_.find(endpoint); iter != std::end(io_connection_diags_))
  {
    if (auto result = iter->second->connect_diagnostics(endpoint, std::move(handle)); !result)
    {
      jewels::log_cerr_error(
        "Failed to connect diagnostics to IO connection {} with error: {}", endpoint, result.error());
      return jewels::unexpected{AbstractCasing::Error::connect_publisher_failure};
    }
    return {};
  }
  return set_handle<void>(endpoint, std::move(handle), true);
}

template <typename... Cogs, typename... Schemas, typename... IoConnections>
jewels::expected<std::shared_ptr<pinion::Observer>, AbstractCasing::Error>
CasingImpl<std::tuple<Cogs...>, std::tuple<Schemas...>, std::tuple<IoConnections...>>::try_connect_subscriber(
  jewels::Uuid<common::EndpointInstanceId> endpoint, std::shared_ptr<pinion::AbstractChannel> channel)
{
  if (auto iter = io_connections_.find(endpoint); iter != std::end(io_connections_))
  {
    auto& [io_connection, endpoint_class_id] = iter->second;
    auto result = io_connection->connect_subscriber(endpoint_class_id, std::move(channel));
    if (!result)
    {
      jewels::log_cerr_error(
        "Failed to connect subscriber to IO connection {} with error: {}", endpoint, result.error());
      return jewels::unexpected{AbstractCasing::Error::connect_subscriber_failure};
    }
    return {*std::move(result)};
  }
  return set_handle<std::shared_ptr<pinion::Observer>>(endpoint, std::move(channel));
}

template <typename... Cogs, typename... Schemas, typename... IoConnections>
jewels::expected<void, AbstractCasing::Error>
CasingImpl<std::tuple<Cogs...>, std::tuple<Schemas...>, std::tuple<IoConnections...>>::set_publisher_handle(
  jewels::Uuid<common::EndpointInstanceId> endpoint, pinion::PublisherHandle handle)
{
  return set_handle<void>(endpoint, std::move(handle), false);
}

template <typename... Cogs, typename... Schemas, typename... IoConnections>
jewels::expected<void, AbstractCasing::Error>
CasingImpl<std::tuple<Cogs...>, std::tuple<Schemas...>, std::tuple<IoConnections...>>::set_subscriber(
  jewels::Uuid<common::EndpointInstanceId> endpoint)
{
  auto endpoint_it = endpoints_.find(endpoint);
  if (endpoint_it != endpoints_.end())
  {
    auto& [cog, class_id] = endpoint_it->second;
    return cog->set_subscriber(class_id).transform_error([](auto&&)
                                                         { return AbstractCasing::Error::invalid_class_uuid; });
  }
  return jewels::unexpected(AbstractCasing::Error::invalid_instance_uuid);
}

template <typename... Cogs, typename... Schemas, typename... IoConnections>
jewels::expected<void, AbstractCasing::Error>
CasingImpl<std::tuple<Cogs...>, std::tuple<Schemas...>, std::tuple<IoConnections...>>::try_connect_state(
  jewels::Uuid<common::EndpointInstanceId> endpoint, jewels::Uuid<common::StateInstanceId> state_id, bool is_shared)
{
  auto state_it = states_.find(state_id);
  if (state_it == states_.end())
  {
    return jewels::unexpected(AbstractCasing::Error::invalid_instance_uuid);
  }
  return set_handle<void>(endpoint, state_it->second, is_shared);
}

template <typename... Cogs, typename... Schemas, typename... IoConnections>
jewels::expected<void, AbstractCasing::Error>
CasingImpl<std::tuple<Cogs...>, std::tuple<Schemas...>, std::tuple<IoConnections...>>::try_connect_config(
  jewels::Uuid<common::EndpointInstanceId> endpoint, jewels::Uuid<common::ConfigInstanceId> config_id)
{
  auto config_it = configs_.find(config_id);
  if (config_it == configs_.end())
  {
    return jewels::unexpected(AbstractCasing::Error::invalid_instance_uuid);
  }
  return set_handle<void>(endpoint, config_it->second);
}

template <typename... Cogs, typename... Schemas, typename... IoConnections>
jewels::expected<std::shared_ptr<pinion::Observer>, AbstractCasing::Error>
CasingImpl<std::tuple<Cogs...>, std::tuple<Schemas...>, std::tuple<IoConnections...>>::try_connect_timer(
  jewels::Uuid<common::EndpointInstanceId> endpoint, std::shared_ptr<AbstractTimer> timer)
{
  return set_handle<std::shared_ptr<pinion::Observer>>(endpoint, std::move(timer));
}

template <typename... Cogs, typename... Schemas, typename... IoConnections>
jewels::expected<void, AbstractCasing::Error>
CasingImpl<std::tuple<Cogs...>, std::tuple<Schemas...>, std::tuple<IoConnections...>>::try_connect_memory_resource(
  jewels::Uuid<common::EndpointInstanceId> endpoint, jewels::memory::MemoryResource memres)
{
  return set_handle<void>(endpoint, memres);
}

template <typename... Cogs, typename... Schemas, typename... IoConnections>
AbstractCasing::SnapshotConfigOutcome
CasingImpl<std::tuple<Cogs...>, std::tuple<Schemas...>, std::tuple<IoConnections...>>::try_configure_snapshot(
  const Tappy<common::SnapshotConfig>& snapshot_config)
{
  auto endpoint_it = endpoints_.find(snapshot_config.get_endpoint_id());
  if (endpoint_it == endpoints_.end())
  {
    return AbstractCasing::SnapshotConfigResult::endpoint_not_found;
  }

  auto& [cog, endpoint_class_id] = endpoint_it->second;

  const auto result = cog->set_snapshot_config(endpoint_class_id, snapshot_config);
  if (jewels::fails(result))
  {
    return AbstractCasing::SnapshotConfigResult::set_handle_failed;
  }

  return AbstractCasing::SnapshotConfigResult::success;
}

template <typename... Cogs, typename... Schemas, typename... IoConnections>
jewels::expected<std::shared_ptr<EPollable>, AbstractCasing::Error>
CasingImpl<std::tuple<Cogs...>, std::tuple<Schemas...>, std::tuple<IoConnections...>>::try_instantiate_io_connection(
  jewels::Uuid<common::IoConnectionClassId> io_connection,
  jewels::Uuid<common::IoConnectionInstanceId> instance_id,
  std::span<const Tappy<common::EndpointInstanceDescription>> endpoints,
  std::optional<jewels::Uuid<common::EndpointInstanceId>> diags_endpoint_id)
{
  if constexpr (sizeof...(IoConnections) > 0)
  {
    return try_instantiate_io_connection<IoConnections...>(io_connection, instance_id, endpoints, diags_endpoint_id);
  }
  else
  {
    return jewels::unexpected(AbstractCasing::Error::invalid_class_uuid);
  }
}

template <typename... Cogs, typename... Schemas, typename... IoConnections>
template <class IoConnection0, class... IoConnectionsN>
jewels::expected<std::shared_ptr<EPollable>, AbstractCasing::Error>
CasingImpl<std::tuple<Cogs...>, std::tuple<Schemas...>, std::tuple<IoConnections...>>::try_instantiate_io_connection(
  jewels::Uuid<common::IoConnectionClassId> io_connection,
  jewels::Uuid<common::IoConnectionInstanceId> instance_id,
  std::span<const Tappy<common::EndpointInstanceDescription>> endpoints,
  std::optional<jewels::Uuid<common::EndpointInstanceId>> diags_endpoint_id)
{
  if (IoConnection0::uuid == io_connection)
  {
    auto maybe_io_connection = IoConnection0::try_make(memres_);
    if (!maybe_io_connection)
    {
      jewels::log_cerr_error(
        "Failed to instantiate IoConnection {} with error: {}", instance_id, maybe_io_connection.error());
      return jewels::unexpected{AbstractCasing::Error::init_failure};
    }
    auto io_connection_ptr = *std::move(maybe_io_connection);
    if (endpoints.empty())
    {
      return jewels::unexpected{AbstractCasing::Error::empty_endpoint_ids};
    }
    for (auto endpoint : endpoints)
    {
      // Each io connection type is aware of the endpoint class ID it
      // should expect for each connection. Thu, we store both the io
      // connection and the class ID so the class ID can be provided
      // at connect time.
      io_connections_.emplace(
        endpoint.get_endpoint_instance_id(), std::make_pair(io_connection_ptr.get(), endpoint.get_endpoint_class_id()));
    }
    if (diags_endpoint_id)
    {
      io_connection_diags_.emplace(*diags_endpoint_id, io_connection_ptr.get());
    }
    if constexpr (detail::is_epollable_v<decltype(io_connection_ptr)>)
    {
      // Pull the shared pointer out of the non-null as not all
      // io-connections are epollable and nullity of the pointer is
      // used to indicate that.
      return io_connection_ptr.get();
    }
    else
    {
      return {};
    }
  }
  if constexpr (sizeof...(IoConnectionsN) > 0)
  {
    return try_instantiate_io_connection<IoConnectionsN...>(io_connection, instance_id, endpoints, diags_endpoint_id);
  }
  else
  {
    return jewels::unexpected(AbstractCasing::Error::invalid_class_uuid);
  }
}

template <typename... Cogs, typename... Schemas, typename... IoConnections>
template <typename RetT, typename... Args>
[[nodiscard]] jewels::expected<RetT, AbstractCasing::Error>
CasingImpl<std::tuple<Cogs...>, std::tuple<Schemas...>, std::tuple<IoConnections...>>::set_handle(
  jewels::Uuid<common::EndpointInstanceId> endpoint, Args&&... args)
{
  auto endpoint_it = endpoints_.find(endpoint);
  if (endpoint_it != endpoints_.end())
  {
    auto& [cog, class_id] = endpoint_it->second;
    return cog->set_handle(class_id, std::forward<Args>(args)...)
      .transform_error([](auto&&) { return AbstractCasing::Error::invalid_class_uuid; });
  }
  return jewels::unexpected(AbstractCasing::Error::invalid_instance_uuid);
}

template <typename... Cogs, typename... Schemas, typename... IoConnections>
jewels::expected<void, jewels::MonoError>
CasingImpl<std::tuple<Cogs...>, std::tuple<Schemas...>, std::tuple<IoConnections...>>::finalize()
{
  auto cag_validate = [](const auto& map_it) { return map_it.second->validate().has_value(); };
  if (std::ranges::all_of(cogs_, cag_validate))
  {
    return {};
  }
  return jewels::unexpected(jewels::MonoError{});
}

template <typename... Cogs, typename... Schemas, typename... IoConnections>
void CasingImpl<std::tuple<Cogs...>, std::tuple<Schemas...>, std::tuple<IoConnections...>>::shutdown()
{
  endpoints_ = {};
  cogs_ = {};
  states_.clear();
  configs_.clear();
  io_connections_.clear();
}

template <typename... Cogs, typename... Schemas, typename... IoConnections>
jewels::expected<void, AbstractCasing::Error>
CasingImpl<std::tuple<Cogs...>, std::tuple<Schemas...>, std::tuple<IoConnections...>>::execute_init_cog(
  jewels::Uuid<common::CogInstanceId> /*unused*/)
{
  return jewels::unexpected(AbstractCasing::Error::invalid_instance_uuid);
}

template <typename... Cogs, typename... Schemas, typename... IoConnections>
jewels::expected<void, AbstractCasing::Error>
CasingImpl<std::tuple<Cogs...>, std::tuple<Schemas...>, std::tuple<IoConnections...>>::start_cog(
  jewels::Uuid<common::CogInstanceId> /*unused*/)
{
  return jewels::unexpected(AbstractCasing::Error::invalid_instance_uuid);
}

template <typename... Cogs, typename... Schemas, typename... IoConnections>
jewels::expected<std::future<void>, AbstractCasing::Error>
CasingImpl<std::tuple<Cogs...>, std::tuple<Schemas...>, std::tuple<IoConnections...>>::stop_cog(
  jewels::Uuid<common::CogInstanceId> /*unused*/)
{
  return jewels::unexpected(AbstractCasing::Error::invalid_instance_uuid);
}

template <typename... Cogs, typename... Schemas, typename... IoConnections>
bool CasingImpl<std::tuple<Cogs...>, std::tuple<Schemas...>, std::tuple<IoConnections...>>::has_schema_representation(
  const jewels::Uuid<RepresentationTag>& repr_id) const noexcept
{
  bool found = false;
  // Each schema type in `Schemas...` exposes its UUID via either
  // `detail::Config::Traits<T>::uuid` (Tap<Tachyon<...>> and ProtoSchema) or
  // `detail::State::Traits<T>::uuid` (CxxSchema, plus Tap<Tachyon<...>>).
  auto check = [&found, repr_id]<typename T>()
  {
    if constexpr (requires { detail::Config::template Traits<T>::uuid; })
    {
      if (detail::Config::template Traits<T>::uuid == repr_id)
      {
        found = true;
      }
    }
    else if constexpr (requires { detail::State::template Traits<T>::uuid; })
    {
      if (detail::State::template Traits<T>::uuid == repr_id)
      {
        found = true;
      }
    }
  };
  (check.template operator()<Schemas>(), ...);
  return found;
}

template <typename... Cogs, typename... Schemas, typename... IoConnections>
bool CasingImpl<std::tuple<Cogs...>, std::tuple<Schemas...>, std::tuple<IoConnections...>>::has_io_connection_class(
  const jewels::Uuid<common::IoConnectionClassId>& io_id) const noexcept
{
  bool found = false;
  auto check = [&found, io_id]<typename T>()
  {
    if (T::uuid == io_id)
    {
      found = true;
    }
  };
  (check.template operator()<IoConnections>(), ...);
  return found;
}

} // namespace clockwork::scaffolding
