// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/common/process_description_clk_cc.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"

#include <cstddef>
#include <functional>
#include <optional>
#include <tuple>
#include <utility>

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
  using PoliciesTuple = std::tuple<Policies...>;
  using MemoryResourcesTuple = std::tuple<typename Policies::MemoryResourceType...>;
  template <size_t index>
  using MemoryResourceRefType =
    std::reference_wrapper<const typename std::tuple_element_t<index, MemoryResourcesTuple>>;

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

  /// Test whether the memory resource at the specified index has been set
  /// @tparam index Record tuple index
  /// @return True if the memory resource has been set
  template <size_t index>
  [[nodiscard]] bool is_memory_resource_set() const;

  /// Get the memory resource at the specified index
  /// @tparam index
  /// @param[out] memres Memory resource reference
  /// @return Success or failure if the memory resource has not been set
  template <size_t index>
  jewels::BinaryOutcome get_memory_resource(jewels::FactoryOut<MemoryResourceRefType<index>> memory_resource) const;

  /// Set the memory resource at the specified index
  /// @tparam index
  /// @param[in] memory_resource Memory resource to set
  /// @return Success of failure if the memory resource has already been set
  template <size_t index>
  jewels::BinaryOutcome set_memory_resource(typename std::tuple_element_t<index, MemoryResourcesTuple> memory_resource);

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
