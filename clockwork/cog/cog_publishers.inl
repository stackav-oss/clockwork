// IWYU pragma: private, include "clockwork/cog/cog_publishers.hh"
#pragma once

#include "clockwork/cog/cog_publishers.hh"

#include "clockwork/cog/detail.hh"
#include "clockwork/common/process_description.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_shared_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"

#include <cstddef>
#include <tuple>
#include <utility>

namespace clockwork
{

template <typename... Policies>
CogPublishers<Policies...>::CogPublishers(jewels::memory::MemoryResource resource) noexcept
  : resource_(std::move(resource))
{
}

template <typename... Policies>
bool CogPublishers<Policies...>::validate() const
{
  auto validate = []<typename Policy>(const Publisher<Policy>& publisher)
  {
    if (!publisher.handle)
    {
      return false;
    }
    const auto expected_msg_size = sizeof(typename Policy::MsgType);
    const auto configured_msg_size = publisher.handle->layout().message_size;
    if (expected_msg_size != configured_msg_size)
    {
      jewels::log_cerr_error(
        "Publisher {} has incorrect message size: got {}, expected {}",
        Policy::name,
        configured_msg_size,
        expected_msg_size);
      return false;
    }
    return true;
  };
  return detail::validate_helper<Publisher>(publishers_, validate);
}

template <typename... Policies>
jewels::expected<void, jewels::MonoError> CogPublishers<Policies...>::set_handle(
  jewels::Uuid<common::EndpointClassId> endpoint_id, pinion::PublisherHandle handle)
{
  auto try_set = [this, &endpoint_id, &handle]<typename Publisher>(Publisher& publisher)
  {
    if (endpoint_id == Publisher::Policy::endpoint_id)
    {
      publisher.handle = jewels::memory::make_pmr_shared<pinion::PublisherHandle>(resource_, std::move(handle));
      return true;
    }
    return false;
  };

  auto is_set = std::apply([&try_set](auto&... pub) -> bool { return (try_set(pub) || ...); }, publishers_);

  if (is_set)
  {
    return {};
  }

  return jewels::unexpected(jewels::MonoError{});
}

template <typename... Policies>
auto CogPublishers<Policies...>::reserve_slots() -> jewels::expected<ReservedSlotsArray, jewels::MonoError>
{
  auto slots =
    std::apply([](auto&... publisher) { return std::make_tuple(publisher.handle->reserve()...); }, publishers_);

  if (!std::apply([](auto&... slot) -> bool { return (static_cast<bool>(slot) && ...); }, slots))
  {
    return jewels::unexpected{jewels::MonoError{}};
  }

  return std::apply([](auto&... slot) -> ReservedSlotsArray { return {std::move(*slot)...}; }, slots);
}

namespace
{
template <typename... Policies, typename ArrayType, std::size_t... indices>
auto make_publishables_impl(ArrayType& slots, std::index_sequence<indices...> /*unused*/)
{
  return std::make_tuple(pinion::Publishable<typename Policies::MsgType>::try_make(
    jewels::memory::make_non_null_from_ref(slots.at(indices)))...);
}
} // namespace

template <typename... Policies>
auto CogPublishers<Policies...>::make_publishables(ReservedSlotsArray& slots)
  -> jewels::expected<PublishablesTuple, jewels::MonoError>
{
  auto publishables = make_publishables_impl<Policies...>(slots, std::make_index_sequence<policy_count>{});

  if (!std::apply([](auto&... publishable) -> bool { return (static_cast<bool>(publishable) && ...); }, publishables))
  {
    return jewels::unexpected{jewels::MonoError{}};
  }

  return std::apply(
    [](auto&... publishable) -> PublishablesTuple { return std::make_tuple(std::move(*publishable)...); },
    publishables);
}

} // namespace clockwork
