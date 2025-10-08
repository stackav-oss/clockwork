// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/common/process_description.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/rate_limiter/token_bucket.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>

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
  using ReservedSlotsArray = std::array<pinion::ReservedSlot, policy_count>;
  using RateLimitersArray = std::array<std::optional<jewels::rate_limiter::TokenBucket>, policy_count>;
  using RateLimitStatusArray = std::array<bool, policy_count>;
  using PublishablesTuple = std::tuple<pinion::Publishable<typename Policies::MsgType>...>;

  /// Construct from a pinion publisher handle.
  explicit CogPublishers(jewels::memory::MemoryResource resource) noexcept;

  /// Validate that all internal types are set.
  [[nodiscard]] bool validate() const;

  /// Set the publisher handle.
  [[nodiscard]] jewels::expected<void, jewels::MonoError>
  set_handle(jewels::Uuid<common::EndpointClassId> endpoint_id, pinion::PublisherHandle handle, bool connected = true);

  /// Reserve new output slots.
  [[nodiscard]] jewels::expected<ReservedSlotsArray, jewels::MonoError> reserve_slots();

  /// Make the publishables tuple.
  [[nodiscard]] jewels::expected<PublishablesTuple, jewels::MonoError> make_publishables(ReservedSlotsArray& slots);

  /// Advance the clock for any rate limited publishers.
  /// After calling this method, you should call `update_throttle_status` before calling it again.
  /// @param current_time The current time.
  void update_rate_limiters(jewels::time::SyncTime current_time);

  /// @returns True if any of the underlying publishers are currently being throttled.
  [[nodiscard]] bool any_throttled() const;

  /// Update rate limiter state for the underlying publishers. If the slot for a publisher has been committed since the
  /// last call to `update_rate_limiters`, it will be pessimistically marked as throttled until the next call to
  /// `update_rate_limiters`.
  /// @param slots The slots from the last call to `reserve_slots()`.
  void update_throttle_status(const ReservedSlotsArray& slots);

  /// Set diagnostics for each output
  template <typename Report, typename Enum, Enum... signal_ids>
  void set_infra_diagnostics(
    Report& report,
    const ReservedSlotsArray& slots,
    jewels::time::SyncTime publish_time,
    std::integer_sequence<Enum, signal_ids...> /*signal_ids*/) const;

private:
  template <typename PolicyT>
  struct Publisher
  {
    using Policy = PolicyT;
    std::shared_ptr<pinion::PublisherHandle> handle;
    bool connected{true};
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
