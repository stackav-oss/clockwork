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
class ClockworkReporter
{
public:
  ClockworkReporter() = default;
  ClockworkReporter(const ClockworkReporter&) = delete;
  ClockworkReporter& operator=(const ClockworkReporter&) = delete;
  ClockworkReporter& operator=(ClockworkReporter&&) = delete;
  ClockworkReporter(ClockworkReporter&& other) noexcept = default;
  ~ClockworkReporter() = default;

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

template <auto group_id_v>
class ClockworkManager
{
public:
  using Reporter = ClockworkReporter<group_id_v>;

  static constexpr auto group_id = group_id_v;

  template <typename ReporterId>
  explicit ClockworkManager(const ReporterId& /*reporter_id*/)
  {
  }

  template <typename InstanceType, typename ReporterId>
  explicit ClockworkManager(InstanceType /*inst*/, const ReporterId& /*reporter_id*/)
  {
  }

  template <typename ReporterId>
  explicit ClockworkManager(std::string_view /*inst*/, const ReporterId& /*reporter_id*/)
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

} // namespace clockwork::diagnostics
