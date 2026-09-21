// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/cog/unit_test_support.hh"
#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/pinion/abstract_channel.hh"
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/publishable.hh"
#include "clockwork/pinion/publisher_slot_ref.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/rate_limiter/token_bucket.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <array>
#include <bitset>
#include <concepts>
#include <cstdint>
#include <memory>
#include <optional>
#include <sys/types.h>
#include <tuple>
#include <utility>
#include <variant>

namespace clockwork
{

/// Token bucket parameters for cog publishers.
struct RateLimitParameters
{
  /// The number of publishes to allow.
  uint64_t limit;
  /// The period to limit over.
  jewels::time::SyncClock::duration period;
};

/// Helper class to handle basic message output handling for a set of publishers. Maintains
/// the necessary bookkeeping and conversion to publishable output types based on the templated policies.
///
/// @tparam Policy structure as follows:
///   struct Policy
///   {
///     // The input message type
///     using MsgType;
///     // The endpoint id of the publisher
///     static constexpr EndpointClassId endpoint_id;
///   };
template <typename... Policies>
class CogPublishers
{
public:
  static constexpr auto policy_count = sizeof...(Policies);
  static constexpr bool has_rate_limits = (Policies::rate_limit_params.has_value() || ...);
  using PoliciesTuple = std::tuple<Policies...>;
  using ReservationArray = std::array<pinion::PublisherReservation, policy_count>;
  using RateLimitersArray = std::array<std::optional<jewels::rate_limiter::TokenBucket>, policy_count>;
  using RateLimitStatusArray = std::array<bool, policy_count>;
  using PublisherThrottleSet = std::bitset<policy_count>;
  using PublishablesTuple = std::tuple<pinion::Publishable<typename Policies::MsgType, Policies::max_msgs_per_exec>...>;
  template <size_t index>
  using PolicyType = std::tuple_element_t<index, PoliciesTuple>;
  template <size_t index>
  using UnitTestOutputViewPolicyType =
    testing::UnitTestCogOutputViewPolicy<typename PolicyType<index>::MsgType, PolicyType<index>>;
  using UnitTestCogOutputViewTuple = std::tuple<testing::UnitTestCogOutputViewPtrType<
    testing::UnitTestCogOutputViewPolicy<typename Policies::MsgType, Policies>>...>;
  /// Construct from a pinion publisher handle.
  explicit CogPublishers(jewels::memory::MemoryResource resource) noexcept;

  /// Validate that all internal types are set.
  [[nodiscard]] bool validate() const;

  /// Validate that all published once outputs have never been published
  [[nodiscard]] bool validate_published_once_outputs() const;

  /// Set the publisher handle.
  [[nodiscard]] jewels::expected<void, jewels::MonoError> set_handle(
    jewels::Uuid<common::EndpointClassId> endpoint_id, pinion::PublisherHandle&& handle, bool connected = true);

  /// Reserve new output slots.
  [[nodiscard]] jewels::expected<ReservationArray, jewels::MonoError> reserve_slots();

  /// Make the publishables tuple.
  [[nodiscard]] jewels::expected<PublishablesTuple, jewels::MonoError>
  make_publishables(ReservationArray& reservations);

  /// Advance the clock for any rate limited publishers and reserve available grants.
  /// After calling this method, you should call `update_throttle_status` before calling it again.
  /// @param[out] throttled_until_out The latest rejected-publisher deadline on failure.
  /// @param[out] throttled_publishers_out The rejected publisher indices on failure.
  /// @param[in] current_time The current time.
  /// @return Success when all outstanding publishers have grants, otherwise failure.
  jewels::BinaryOutcome update_rate_limiters(
    jewels::Out<jewels::time::SyncTime> throttled_until_out,
    jewels::Out<PublisherThrottleSet> throttled_publishers_out,
    jewels::time::SyncTime current_time);

  /// Update rate limiter state for the underlying publishers. If the slot for a publisher has been committed since the
  /// last call to `update_rate_limiters`, it will be pessimistically marked as throttled until the next call to
  /// `update_rate_limiters`.
  /// @param slots The slots from the last call to `reserve_slots()`.
  void update_throttle_status(const ReservationArray& reservations);

  /// Set diagnostics for each output
  template <typename Report, typename Enum, Enum... signal_ids>
  void set_infra_diagnostics(
    Report& report,
    const ReservationArray& reservations,
    jewels::time::SyncTime publish_time,
    std::integer_sequence<Enum, signal_ids...> /*signal_ids*/) const;

  /// Set the publisher handle at the specified index
  ///
  /// Used by unit test cogs to initialize the unit test output and metrics channels
  ///
  /// @tparam<index> Publisher index
  /// @param[in] handle Publisher handle
  template <size_t index>
  void set_unit_test_publisher(pinion::PublisherHandle&& handle);

private:
  template <typename PolicyT>
  struct Publisher
  {
    using Policy = PolicyT;
    std::shared_ptr<pinion::PublisherHandle> handle;
    bool connected{true};

    [[nodiscard]] jewels::expected<pinion::PublisherReservation, pinion::ReserveError> reserve() const;
  };

  using PublishersTuple = std::tuple<Publisher<Policies>...>;

  /// Memory resource
  jewels::memory::MemoryResource resource_;
  /// The publisher handles
  PublishersTuple publishers_;
  RateLimitersArray rate_limiters_;
  RateLimitStatusArray publishers_throttled_;
};

} // namespace clockwork

#include "clockwork/cog/cog_publishers.inl"
