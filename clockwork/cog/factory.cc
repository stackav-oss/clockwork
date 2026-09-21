// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/cog/factory.hh"

#include "jewels/container/compare.hh"

namespace clockwork
{
namespace detail
{
template <typename FactoryType, typename IdType>
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) Initializing a static member variable.
FactoryType* ClassFactoryRegistry<FactoryType, IdType>::head_ = nullptr;

template <typename FactoryType, typename IdType>
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) Initializing a static member variable.
FactoryType* ClassFactoryRegistry<FactoryType, IdType>::tail_ = nullptr;

template <typename FactoryType, typename IdType>
ClassFactoryRegistry<FactoryType, IdType>::ClassFactoryRegistry()
  : prev_(tail_), next_(nullptr)
{
  auto* self = static_cast<FactoryType*>(this);
  if (head_ == nullptr)
  {
    head_ = self;
  }
  if (prev_)
  {
    prev_->next_ = self;
  }
  tail_ = self;
}

template <typename FactoryType, typename IdType>
ClassFactoryRegistry<FactoryType, IdType>::~ClassFactoryRegistry()
{
  if (prev_)
  {
    prev_->next_ = next_;
  }
  if (next_)
  {
    next_->prev_ = prev_;
  }
  if (head_ == this)
  {
    head_ = next_;
  }
  if (tail_ == this)
  {
    tail_ = prev_;
  }
}

template <typename FactoryType, typename IdType>
const FactoryType* ClassFactoryRegistry<FactoryType, IdType>::find(const IdType& class_id)
{
  for (const FactoryType* iter = head_; iter != nullptr; iter = iter->next_)
  {
    if (iter->id() == class_id)
    {
      return iter;
    }
  }
  return nullptr;
}

template class detail::ClassFactoryRegistry<CogFactory, jewels::Uuid<common::CogClassId>>;
template class detail::ClassFactoryRegistry<CogStateFactory, jewels::Uuid<RepresentationTag>>;

} // namespace detail

CogFactory::CogFactory() = default;

CogStateFactory::CogStateFactory() = default;

CogStateFactory::Ptr
CogStateFactory::make(jewels::memory::MemoryResource /*memres_sys*/, pinion::PublisherHandle /*publisher*/) const
{
  return nullptr;
}

CogStateFactory::Ptr CogStateFactory::make(
  jewels::memory::MemoryResource /*memres_sys*/, jewels::memory::MemoryResource /*memres_state*/) const
{
  return nullptr;
}

CogStateFactory::StateRestoreOutcome CogStateFactory::make(
  jewels::Out<Ptr> /*state_out*/,
  jewels::memory::MemoryResource /*memres_sys*/,
  jewels::memory::MemoryResource /*memres_state*/,
  jewels::Uuid<RepresentationTag> /*snapshot_representation_id*/,
  std::span<const std::byte> /*snapshot_data*/) const
{
  return StateRestoreResult::invalid_class_uuid;
}

} // namespace clockwork
