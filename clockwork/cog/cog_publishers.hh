// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/common/process_description.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"

#include <array>
#include <memory>
#include <tuple>

namespace clockwork
{

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
  using PublishablesTuple = std::tuple<pinion::Publishable<typename Policies::MsgType>...>;

  /// Construct from a pinion publisher handle.
  explicit CogPublishers(jewels::memory::MemoryResource resource) noexcept;

  /// Validate that all internal types are set.
  [[nodiscard]] bool validate() const;

  /// Set the publisher handle.
  [[nodiscard]] jewels::expected<void, jewels::MonoError>
  set_handle(jewels::Uuid<common::EndpointClassId> endpoint_id, pinion::PublisherHandle handle);

  /// Reserve new output slots.
  [[nodiscard]] jewels::expected<ReservedSlotsArray, jewels::MonoError> reserve_slots();

  /// Make the publishables tuple.
  [[nodiscard]] jewels::expected<PublishablesTuple, jewels::MonoError> make_publishables(ReservedSlotsArray& slots);

private:
  template <typename PolicyT>
  struct Publisher
  {
    using Policy = PolicyT;
    std::shared_ptr<pinion::PublisherHandle> handle;
  };

  using PublishersTuple = std::tuple<Publisher<Policies>...>;

  /// Memory resource
  jewels::memory::MemoryResource resource_;
  /// The publisher handles
  PublishersTuple publishers_;
};

} // namespace clockwork

#include "clockwork/cog/cog_publishers.inl"
