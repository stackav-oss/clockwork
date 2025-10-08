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
#include "jewels/meta/types.hh"
#include "jewels/rate_limiter/token_bucket.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <optional>
#include <tuple>
#include <utility>

namespace clockwork
{

namespace detail
{
template <typename Policy>
std::optional<jewels::rate_limiter::TokenBucket> maybe_make_rate_limiter(jewels::meta::Types<Policy> /*unused*/)
{
  if constexpr (Policy::rate_limit_params)
  {
    return jewels::rate_limiter::TokenBucket(Policy::rate_limit_params->limit, Policy::rate_limit_params->period);
  }
  return std::nullopt;
}
} // namespace detail

template <typename... Policies>
CogPublishers<Policies...>::CogPublishers(jewels::memory::MemoryResource resource) noexcept
  : resource_(std::move(resource)), rate_limiters_{detail::maybe_make_rate_limiter(jewels::meta::Types<Policies>{})...}
{
  // Pessimistically assume we are throttled to start. This will prompt us to
  // query the rate limiter imediately.
  publishers_throttled_.fill(true);
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
  jewels::Uuid<common::EndpointClassId> endpoint_id, pinion::PublisherHandle handle, bool connected)
{
  auto try_set = [this, &endpoint_id, &handle, connected]<typename Publisher>(Publisher& publisher)
  {
    if (endpoint_id == Publisher::Policy::endpoint_id)
    {
      publisher.handle = jewels::memory::make_pmr_shared<pinion::PublisherHandle>(resource_, std::move(handle));
      publisher.connected = connected;
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
  auto slots = std::apply(
    [](auto&... publisher) { return std::make_tuple(publisher.handle->reserve(publisher.connected)...); }, publishers_);

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
  return std::make_tuple(
    pinion::Publishable<typename Policies::MsgType>::try_make(
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

template <typename... Policies>
void CogPublishers<Policies...>::update_rate_limiters(const jewels::time::SyncTime current_time)
{
  for (size_t i = 0; i < rate_limiters_.size(); i++)
  {
    auto& limiter = rate_limiters_.at(i);
    auto& throttled = publishers_throttled_.at(i);
    if (!limiter)
    {
      // Publishers without rate limiters should never contribute to the cog being throttled.
      throttled = false;
      continue;
    }

    // If we published in the last cycle, this flag will be set. That is, we
    // assume the cog has been throttled until told otherwise. However, if the
    // cog was granted work by the rate limiter in the last cycle but didn't end
    // up publishing, the grant can be saved for later. It doesn't expire.

    if (throttled)
    {
      throttled = !(*limiter)(current_time);
    }
  }
}

template <typename... Policies>
bool CogPublishers<Policies...>::any_throttled() const
{
  return std::any_of(publishers_throttled_.begin(), publishers_throttled_.end(), std::identity{});
}

template <typename... Policies>
void CogPublishers<Policies...>::update_throttle_status(const ReservedSlotsArray& slots)
{
  for (size_t i = 0; i < slots.size(); i++)
  {
    auto& slot = slots.at(i);
    auto& limiter = rate_limiters_.at(i);
    auto& throttled = publishers_throttled_.at(i);
    if (limiter && !throttled && slot.state() == pinion::ReservationState::State::commit)
    {
      // Something was published to this buffer during this cycle. Signal that
      // we should check with the rate limiter in the next cycle.

      throttled = true;
    }
  }
}

template <typename... Policies>
template <typename Report, typename Enum, Enum... signal_ids>
void CogPublishers<Policies...>::set_infra_diagnostics(
  Report& report,
  const ReservedSlotsArray& slots,
  jewels::time::SyncTime publish_time,
  std::integer_sequence<Enum, signal_ids...> /*signal_ids*/) const
{
  static_assert(sizeof...(signal_ids) <= sizeof...(Policies));
  auto dispatch = [&]<
                    Enum signal_id0,
                    Enum... signal_id,
                    size_t policy_idx0,
                    size_t... policy_idx,
                    typename Policy0,
                    typename... Policy>(
                    auto& self,
                    std::integer_sequence<Enum, signal_id0, signal_id...> /*signal_id_seq*/,
                    std::index_sequence<policy_idx0, policy_idx...> /*policy_idx_seq*/,
                    std::tuple<Policy0, Policy...> /*policy_seq*/)
  {
    if constexpr (Policy0::has_diagnostics)
    {
      static_assert(sizeof...(signal_id) > 0, "diagnostic signal and Policy::has_diagnostics count mismatch");
      const size_t count =
        (std::get<policy_idx0>(slots).state() == pinion::ReservationState::State::commit ? 1UL : 0UL);
      report.template set<signal_id0>({.timestamp = publish_time, .count = count});
      if constexpr (sizeof...(Policy) > 0)
      {
        self(
          self,
          std::integer_sequence<Enum, signal_id...>{},
          std::index_sequence<policy_idx...>{},
          std::tuple<Policy...>{});
      }
    }
    else
    {
      if constexpr (sizeof...(Policy) > 0)
      {
        self(
          self,
          std::integer_sequence<Enum, signal_id0, signal_id...>{},
          std::index_sequence<policy_idx...>{},
          std::tuple<Policy...>{});
      }
      else
      {
        static_assert(sizeof...(signal_id) == 0);
      }
    }
  };
  // Note this injects an additional signal_id at the end of the signal_id sequence.  This allows for the `dispatch`
  // lambda to recurse through all the Policies even if the nominal signal_ids are exhausted.  Code structure (and
  // static_asserts) guard against using the injected id.
  if constexpr (sizeof...(Policies) > 0)
  {
    dispatch(
      dispatch,
      std::integer_sequence<Enum, signal_ids..., Enum{}>{},
      std::index_sequence_for<Policies...>{},
      std::tuple<Policies...>{});
  }
}

} // namespace clockwork
