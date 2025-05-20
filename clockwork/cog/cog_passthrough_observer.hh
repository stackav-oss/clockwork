// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/pinion/observer.hh"
#include "jewels/memory/pointers.hh"

namespace clockwork
{

/// Observer that just notifies the underlying cog.
/// @tparam CogType The cog type
template <typename CogType>
class CogPassthroughObserver : public pinion::Observer
{
public:
  explicit CogPassthroughObserver(jewels::memory::ObjectPtr<CogType> cog);
  ~CogPassthroughObserver() override = default;
  CogPassthroughObserver(const CogPassthroughObserver&) = delete;
  CogPassthroughObserver& operator=(const CogPassthroughObserver&) = delete;
  CogPassthroughObserver(CogPassthroughObserver&&) = delete;
  CogPassthroughObserver& operator=(CogPassthroughObserver&&) = delete;
  void notify(const Event& event) override;

private:
  jewels::memory::ObjectPtr<CogType> cog_;
};

} // namespace clockwork

#include "clockwork/cog/cog_passthrough_observer.inl"
