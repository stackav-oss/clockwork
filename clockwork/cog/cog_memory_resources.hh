// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/common/process_description.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"

#include <optional>
#include <tuple>

namespace clockwork
{

/// Helper class to handle the set of cog memory_resources. Maintains the necessary bookkeeping and
/// conversion to dial input types based on the templated policies.
///
/// @tparam Policies see MessageMemoryResource::Policy
template <typename... Policies>
class CogMemoryResources
{
public:
  static constexpr auto policy_count = sizeof...(Policies);
  using MemoryResourcesTuple = std::tuple<typename Policies::MemoryResourceType...>;

  /// Validate that all internal types are set.
  [[nodiscard]] bool validate() const;

  /// Set the memory_resource handle.
  /// @tparam MemoryResourceType The memory_resource type setting.
  template <typename MemoryResourceType>
  [[nodiscard]] jewels::expected<void, jewels::MonoError>
  set_handle(jewels::Uuid<common::EndpointClassId> endpoint_id, MemoryResourceType memory_resource);

  /// Construct the memory_resources tuple.
  /// @return Tuple of memory_resourceuration objects.
  [[nodiscard]] MemoryResourcesTuple make_memory_resources();

private:
  template <typename Policy>
  struct Record
  {
    using PolicyType = Policy;
    using MemoryResourceType = typename Policy::MemoryResourceType;
    std::optional<MemoryResourceType> memory_resource;
  };

  using RecordsTuple = std::tuple<Record<Policies>...>;

  /// The memory resource records
  RecordsTuple records_;
};

} // namespace clockwork

#include "clockwork/cog/cog_memory_resources.inl"
