// IWYU pragma: private, include "clockwork/cog/cog_diagnostics.hh"
#pragma once

#include "clockwork/cog/cog_diagnostics.hh"

#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/diagnostics/report_clk_cc.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"
#include "jewels/uuid/uuid5.hh"

#include <cstddef>
#include <tuple>
#include <utility>

namespace clockwork
{

template <typename Policy>
CogDiagnosticsImpl<Policy>::CogDiagnosticsImpl(const jewels::Uuid<common::CogInstanceId>& instance_id) noexcept
  : manager_(create_manager(instance_id))
{
}

template <typename Policy>
auto CogDiagnosticsImpl<Policy>::create_manager(const jewels::Uuid<common::CogInstanceId>& instance_id) -> ManagerType
{
  const auto reporter_id = jewels::uuid5<diagnostics::ReporterId>(instance_id, Policy::member_name);
  if constexpr (Policy::instance_name.empty())
  {
    return ManagerType(reporter_id);
  }
  else
  {
    return ManagerType(Policy::instance_name, reporter_id);
  }
}

template <typename Policy>
[[nodiscard]] bool CogDiagnosticsImpl<Policy>::validate() const
{
  if (!manager_.publisher().validate())
  {
    jewels::log_cerr_error("cog validation failure: {}", Policy::group_name);
    return false;
  }
  return true;
}

template <typename Policy>
[[nodiscard]] bool CogDiagnosticsImpl<Policy>::set_handle(
  jewels::Uuid<common::EndpointClassId> endpoint_id, pinion::PublisherHandle&& handle)
{
  if (endpoint_id == Policy::endpoint_id)
  {
    manager_.publisher().set_handle(std::move(handle));
    return true;
  }
  return false;
}

template <typename Policy>
[[nodiscard]] auto CogDiagnosticsImpl<Policy>::make_report(jewels::time::SyncTime now) -> ReporterType
{
  return manager_.create_report(now);
}

template <typename Policy>
void CogDiagnosticsImpl<Policy>::set_unit_test_diagnostics_impl(pinion::PublisherHandle&& handle)
{
  manager_.publisher().set_handle(std::move(handle));
}

template <typename... Policies>
CogDiagnostics<Policies...>::CogDiagnostics(const jewels::Uuid<common::CogInstanceId>& instance_id) noexcept
  : impls_((std::ignore = Policies{}, instance_id)...)
{
}

template <typename... Policies>
[[nodiscard]] bool CogDiagnostics<Policies...>::validate() const
{
  return (std::get<CogDiagnosticsImpl<Policies>>(impls_).validate() && ...);
}

template <typename... Policies>
[[nodiscard]] jewels::expected<void, jewels::MonoError> CogDiagnostics<Policies...>::set_handle(
  jewels::Uuid<common::EndpointClassId> endpoint_id, pinion::PublisherHandle&& handle)
{
  // NOLINTNEXTLINE(bugprone-use-after-move) False positive
  return (std::get<CogDiagnosticsImpl<Policies>>(impls_).set_handle(endpoint_id, std::move(handle)) || ...)
           ? jewels::expected<void, jewels::MonoError>{}
           : jewels::unexpected(jewels::MonoError{});
}

template <typename... Policies>
[[nodiscard]] auto CogDiagnostics<Policies...>::make_report(jewels::time::SyncTime now) -> ReporterType
{
  if constexpr (sizeof...(Policies) == 1)
  {
    return std::get<0>(impls_).make_report(now);
  }
  else
  {
    return ReporterType(std::move(std::get<CogDiagnosticsImpl<Policies>>(impls_).make_report(now))...);
  }
}

template <typename... Policies>
void CogDiagnostics<Policies...>::commit(ReporterType& reports, jewels::time::SyncTime now)
{
  if constexpr (sizeof...(Policies) == 1)
  {
    reports.publish(now);
  }
  else
  {
    std::apply([&now](auto&... report) { (report.publish(now), ...); }, reports);
  }
}

template <typename... Policies>
template <size_t index>
void CogDiagnostics<Policies...>::set_unit_test_diagnostics(pinion::PublisherHandle&& handle)
{
  std::get<index>(impls_).set_unit_test_diagnostics_impl(std::move(handle));
}

} // namespace clockwork
