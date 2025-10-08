// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <limits>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

namespace clockwork::diagnostics
{

template <auto group_id_v>
struct GroupProxy
{
};

template <typename Group>
class ClockworkReporterImpl
{
public:
  ClockworkReporterImpl() = default;
  ClockworkReporterImpl(const ClockworkReporterImpl&) = delete;
  ClockworkReporterImpl& operator=(const ClockworkReporterImpl&) = delete;
  ClockworkReporterImpl& operator=(ClockworkReporterImpl&&) = delete;
  ClockworkReporterImpl(ClockworkReporterImpl&& other) noexcept = default;
  ~ClockworkReporterImpl() = default;

  template <auto id, typename Type>
  void set(Type /*value*/)
  {
  }

  template <typename Timestamp>
  void publish(Timestamp /*now*/)
  {
  }
};

struct ClockworkPublisher
{
  [[nodiscard]] static bool validate()
  {
    return true;
  }

  template <typename Type>
  void set_handle(const Type& /*unused*/)
  {
  }
};

template <typename Group>
class ClockworkManagerImpl
{
public:
  using Reporter = ClockworkReporterImpl<Group>;

  static constexpr auto group_id = 1;

  template <typename ReporterId>
  explicit ClockworkManagerImpl(const ReporterId& /*reporter_id*/)
  {
  }

  template <typename InstanceType, typename ReporterId>
  explicit ClockworkManagerImpl(InstanceType /*inst*/, const ReporterId& /*reporter_id*/)
  {
  }

  template <typename ReporterId>
  explicit ClockworkManagerImpl(std::string_view /*inst*/, const ReporterId& /*reporter_id*/)
  {
  }

  template <typename Timestamp>
  Reporter create_report(const Timestamp& /*timestamp*/)
  {
    return {};
  }

  [[nodiscard]] ClockworkPublisher publisher() const
  {
    return {};
  }
};

template <auto group_id_v>
using ClockworkManager = ClockworkManagerImpl<GroupProxy<group_id_v>>;
template <typename Group>
using ClockworkManagerStruct = ClockworkManagerImpl<Group>;
template <auto group_id_v>
using ClockworkReporter = typename ClockworkManagerImpl<GroupProxy<group_id_v>>::Reporter;
template <typename Group>
using ClockworkReporterStruct = typename ClockworkManagerImpl<Group>::Reporter;

} // namespace clockwork::diagnostics
