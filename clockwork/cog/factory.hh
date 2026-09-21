// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/cog/interface.hh"
#include "clockwork/common/forward.hh"
#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/pinion/abstract_channel.hh"
#include "clockwork/tags.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/uuid/uuid.hh"

#include <wise_enum.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace clockwork
{

namespace detail
{
///
/// CRTP helper for registering factories globally with a linked list
///
template <typename FactoryType, typename IdTypeIn>
class ClassFactoryRegistry
{
  ClassFactoryRegistry();

public:
  using IdType = IdTypeIn;

  // NOLINTNEXTLINE(bugprone-crtp-constructor-accessibility) Conflicts with modernize-use-equals-delete
  ClassFactoryRegistry(const ClassFactoryRegistry&) = delete;
  // NOLINTNEXTLINE(bugprone-crtp-constructor-accessibility) Conflicts with modernize-use-equals-delete
  ClassFactoryRegistry(ClassFactoryRegistry&&) = delete;

  virtual ~ClassFactoryRegistry();
  ClassFactoryRegistry& operator=(const ClassFactoryRegistry&) = delete;
  ClassFactoryRegistry& operator=(ClassFactoryRegistry&&) = delete;

  /// Gets the class id of this factory
  [[nodiscard]] virtual const IdType& id() const = 0;

  /// Looks up a factory with the given id
  /// @return The first factory for which `id()==class_id` or nullptr if none were found
  static const FactoryType* find(const IdType& class_id);

private:
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) Static to allow singleton semantics
  static FactoryType* head_;
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) Static to allow singleton semantics
  static FactoryType* tail_;
  FactoryType* prev_;
  FactoryType* next_;

  friend FactoryType;
};

} // namespace detail

struct CogFactory : detail::ClassFactoryRegistry<CogFactory, jewels::Uuid<common::CogClassId>>
{
  using Type = CogBase;
  using Ptr = std::shared_ptr<Type>;

  CogFactory();

  /// Creates an instance of the cog
  /// @param[in] resource The memory resource
  /// @param[in] instance_id The uuid of this cog instance
  /// @param[in] queue The queue to use when the cog is ready to execute.
  [[nodiscard]] virtual Ptr make(
    jewels::memory::MemoryResource resource,
    const jewels::Uuid<common::CogInstanceId>& instance_id,
    jewels::memory::ObjectPtr<AbstractCogQueue> queue) const = 0;
};

struct CogStateFactory : detail::ClassFactoryRegistry<CogStateFactory, jewels::Uuid<RepresentationTag>>
{
  using Type = CogStateData;
  using Ptr = std::shared_ptr<Type>;

  WISE_ENUM_CLASS_MEMBER(
    (StateRestoreResult, uint8_t), (success, 0), (invalid_class_uuid, 1), (buffer_error, 3), (init_failure, 4))
  using StateRestoreOutcome = jewels::Outcome<StateRestoreResult, StateRestoreResult::success>;

  CogStateFactory();

  [[nodiscard]] virtual Ptr make(jewels::memory::MemoryResource memres_sys, pinion::PublisherHandle publisher) const;

  [[nodiscard]] virtual Ptr
  make(jewels::memory::MemoryResource memres_sys, jewels::memory::MemoryResource memres_state) const;

  /// Restores an external state from its serialized snapshot.
  /// @param state_out The restored state object.
  /// @param memres_sys The system memory resource used for the state record.
  /// @param memres_state The memory resource used by the external state.
  /// @param snapshot_representation_id The representation of snapshot_data.
  /// @param snapshot_data The serialized state snapshot.
  /// @return The restoration result.
  [[nodiscard]] virtual StateRestoreOutcome make(
    jewels::Out<Ptr> state_out,
    jewels::memory::MemoryResource memres_sys,
    jewels::memory::MemoryResource memres_state,
    jewels::Uuid<RepresentationTag> snapshot_representation_id,
    std::span<const std::byte> snapshot_data) const;
};

/// Explicit instantiation in order to ensure unique location of static class variables
extern template class detail::ClassFactoryRegistry<CogFactory, jewels::Uuid<common::CogClassId>>;
extern template class detail::ClassFactoryRegistry<CogStateFactory, jewels::Uuid<RepresentationTag>>;

} // namespace clockwork
