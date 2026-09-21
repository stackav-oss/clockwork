// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/cog/unit_test_support.hh"
#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/diagnostics/report_clk_cc.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <cstddef>
#include <tuple>

namespace clockwork
{

namespace pinion
{
class PublisherHandle;
}

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

  /// Set the execution health on the report.
  void set_health(ReporterType& report, diagnostics::ReportHealth health);

  /// Set the publisher handle for the diagnostics at the specified index
  ///
  /// Used by unit test cogs to initialize the unit test diagnostics
  ///
  /// @param[in] handle Publisher handle
  void set_unit_test_diagnostics_impl(pinion::PublisherHandle&& handle);

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
  static constexpr auto policy_count = sizeof...(Policies);
  using PoliciesTuple = std::tuple<Policies...>;
  template <size_t index>
  using PolicyType = std::tuple_element_t<index, PoliciesTuple>;
  template <size_t index>
  using UnitTestOutputViewPolicyType =
    testing::UnitTestCogOutputViewPolicy<Tappy<diagnostics::Report>, PolicyType<index>>;
  using UnitTestCogOutputViewTuple = std::tuple<testing::UnitTestCogOutputViewPtrType<
    testing::UnitTestCogOutputViewPolicy<Tappy<diagnostics::Report>, Policies>>...>;

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

  /// Set the execution health on all reports.
  void set_health(ReporterType& reports, diagnostics::ReportHealth health);

  /// Publish the diagnostics immediately with the provided publish time
  /// @param[in] reports reports to publish
  /// @param[in] now time to provide as the publish time
  static void commit(ReporterType& reports, jewels::time::SyncTime now);

  /// Set the publisher handle for the diagnostics at the specified index
  ///
  /// Used by unit test cogs to initialize the unit test diagnostics
  ///
  /// @tparam<index> Diagnostics index
  /// @param[in] handle Publisher handle
  template <size_t index>
  void set_unit_test_diagnostics(pinion::PublisherHandle&& handle);

private:
  std::tuple<CogDiagnosticsImpl<Policies>...> impls_;
};

} // namespace clockwork

#include "clockwork/cog/cog_diagnostics.inl"
