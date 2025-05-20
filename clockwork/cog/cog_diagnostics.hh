// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/common/process_description.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <cstddef>
#include <tuple>

namespace clockwork
{
namespace detail
{
template <size_t n, typename... Types>
struct TupleOrType
{
  using Type = std::tuple<Types...>;
};
template <typename Type0>
struct TupleOrType<1, Type0>
{
  using Type = Type0;
};

} // namespace detail

/// Helper class to handle cog diagnostics
template <typename Policy>
class CogDiagnosticsImpl
{
public:
  using ManagerType = typename Policy::ManagerType;
  using ReporterType = typename Policy::ManagerType::Reporter;

  /// Construct the diagnostics helper.
  /// @param[in] instance_id The id of the cog instance, to be used as the reporter id
  explicit CogDiagnosticsImpl(const jewels::Uuid<common::CogInstanceId>& instance_id) noexcept;

  /// Validate that all internal types are set.
  [[nodiscard]] bool validate() const;

  /// Set the publisher handle if the id matches.
  /// @param[in] endpoint_id The id
  /// @param[in] handle The publisher, moved from if this succeeds
  /// @return success if the id matches, failure if the id doesn't match
  [[nodiscard]] bool set_handle(jewels::Uuid<common::EndpointClassId> endpoint_id, pinion::PublisherHandle&& handle);

  /// Construct the Reporter for the cog diagnostics
  /// @param[in] now The current time.
  /// @return the reporter
  [[nodiscard]] ReporterType make_report(jewels::time::SyncTime now);

private:
  static ManagerType create_manager(const jewels::Uuid<common::CogInstanceId>& instance_id);

  ManagerType manager_;
};

/// Container for the set of diagnostics for the cog
template <typename... Policies>
class CogDiagnostics
{
public:
  using ReporterType =
    detail::TupleOrType<sizeof...(Policies), typename CogDiagnosticsImpl<Policies>::ReporterType...>::Type;

  /// Construct the diagnostics helper.
  /// @param[in] instance_id The id of the cog instance, to be used as the reporter id
  explicit CogDiagnostics(const jewels::Uuid<common::CogInstanceId>& instance_id) noexcept;

  /// Validate that all internal types are set.
  [[nodiscard]] bool validate() const;

  /// Set the publisher handle if the id matches.
  /// @param[in] endpoint_id The id
  /// @param[in] handle The publisher, moved from if this succeeds
  /// @return success if the id matches, failure if the id doesn't match
  [[nodiscard]] jewels::expected<void, jewels::MonoError>
  // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved) it's conditional
  set_handle(jewels::Uuid<common::EndpointClassId> endpoint_id, pinion::PublisherHandle&& handle);

  /// Construct the Reporter for the cog diagnostics
  /// @param[in] now The current time.
  /// @return the reporter
  [[nodiscard]] ReporterType make_report(jewels::time::SyncTime now);

  /// Publish the diagnostics immediately with the provided publish time
  /// @param[in] reports reports to publish
  /// @param[in] now time to provide as the publish time
  static void commit(ReporterType& reports, jewels::time::SyncTime now);

private:
  std::tuple<CogDiagnosticsImpl<Policies>...> impls_;
};

} // namespace clockwork

#include "clockwork/cog/cog_diagnostics.inl"
