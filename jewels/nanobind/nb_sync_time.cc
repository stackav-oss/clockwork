// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/time/conversions.hh"
#include "jewels/time/sync_time.hh"

#include <fmt/format.h>
#include <nanobind/nanobind.h>
#include <nanobind/operators.h>
#include <nanobind/stl/optional.h> // IWYU pragma: keep
#include <nanobind/stl/string.h>   // IWYU pragma: keep
#include <nanobind/stl/variant.h>  // IWYU pragma: keep

#include <chrono>
#include <cstdint>
#include <new>
#include <string>

namespace nb = nanobind;
namespace clockwork
{

namespace chrono = std::chrono;

using SyncTime = jewels::time::SyncTime;
using Duration = chrono::nanoseconds;

namespace
{

/// Generic function facilitating the std::chrono::time_point operator- overloads
template <typename TimePoint, typename Other>
auto subtract(const TimePoint& time_point, const Other& other) -> decltype(time_point - other)
{
  return time_point - other;
}

/// Generic function facilitating the std::chrono::Duration operator* overloads
template <typename Lhs, typename Rhs>
auto multiply(const Lhs& lhs, Rhs rhs) -> Duration
{
  return chrono::duration_cast<Duration>(lhs * rhs);
}

/// Generic function overload set facilitating the std::chrono::Duration operator/ overloads
/// @{
template <typename Rhs>
auto divide(const Duration& lhs, Rhs rhs) -> Duration
{
  return chrono::duration_cast<Duration>(lhs / rhs);
}

template <typename Rhs>
auto divide(const Duration& lhs, const Duration& rhs) -> decltype(lhs / rhs)
{
  return lhs / rhs;
}
/// @}

} // namespace

nb::class_<SyncTime> bind_sync_time(nb::handle scope)
{
  return nb::class_<SyncTime>(scope, "SyncTime")
    .def(
      "__init__",
      [](SyncTime& self, double seconds)
      {
        new (&self) SyncTime();
        self += chrono::duration_cast<chrono::nanoseconds>(chrono::duration<double, chrono::seconds::period>{seconds});
      },
      nb::kw_only(),
      nb::arg("seconds"),
      "Construct a SyncTime from float seconds since epoch.")
    .def(
      "__init__",
      [](SyncTime& self, int64_t nanoseconds)
      {
        new (&self) SyncTime();
        self = jewels::time::sync_time_from_ns(nanoseconds);
      },
      nb::kw_only(),
      nb::arg("nanoseconds") = 0,
      "Construct a SyncTime from integer nanoseconds since epoch.")
    .def(nb::init<const SyncTime&>(), "Copy constructor.")
    .def(
      "to_ns",
      [](const SyncTime& sync_time) { return jewels::time::get_ns(sync_time); },
      "Convert to nanoseconds (int).")
    .def(
      "to_s",
      [](const SyncTime& sync_time) { return jewels::time::to_seconds<double>(sync_time.time_since_epoch()); },
      "Convert to seconds (float).")
    .def(
      "__eq__",
      [](const SyncTime& self, nb::handle other) -> bool
      {
        try
        {
          const auto other_sync_time = nb::cast<SyncTime>(other);
          return self == other_sync_time;
        }
        catch (nb::cast_error&)
        {
          return false;
        }
      },
      nb::arg("other"),
      nb::sig("def __eq__(self: SyncTime, other: object) -> bool"))
    .def(nb::self > nb::self)  // NOLINT(misc-redundant-expression) Definition of an operation, not an expression.
    .def(nb::self >= nb::self) // NOLINT(misc-redundant-expression) Definition of an operation, not an expression.
    .def(nb::self < nb::self)  // NOLINT(misc-redundant-expression) Definition of an operation, not an expression.
    .def(nb::self <= nb::self) // NOLINT(misc-redundant-expression) Definition of an operation, not an expression.
    .def("__add__", [](const SyncTime& self, const Duration& other) -> SyncTime { return self + other; })
    .def("__sub__", nb::overload_cast<const SyncTime&, const SyncTime&>(&subtract<SyncTime, SyncTime>))
    .def("__sub__", nb::overload_cast<const SyncTime&, const Duration&>(&subtract<SyncTime, Duration>))
    .def(
      "__repr__",
      [](const SyncTime& sync_time) -> std::string
      { return fmt::format("SyncTime({}ns)", jewels::time::get_ns(sync_time)); })
    .def("__getstate__", [](const SyncTime& self) -> int64_t { return jewels::time::get_ns(self); })
    .def(
      "__setstate__", [](SyncTime& self, int64_t nanoseconds) { self = SyncTime(chrono::nanoseconds(nanoseconds)); });
}

nb::class_<Duration> bind_duration(nb::handle scope)
{
  return nb::class_<Duration>(scope, "Duration")
    .def(
      "__init__",
      [](Duration& self, double seconds)
      {
        new (&self) chrono::nanoseconds();
        self = chrono::duration_cast<chrono::nanoseconds>(chrono::duration<double, chrono::seconds::period>{seconds});
      },
      nb::kw_only(),
      nb::arg("seconds"),
      "Construct a Duration from float seconds.")
    .def(
      "__init__",
      [](Duration& self, int64_t nanoseconds)
      {
        new (&self) chrono::nanoseconds();
        self = chrono::nanoseconds(nanoseconds);
      },
      nb::kw_only(),
      nb::arg("nanoseconds") = 0,
      "Construct a Duration from integer nanoseconds.")
    .def(nb::init<const Duration&>(), "Copy constructor.")
    .def(
      "to_s",
      [](const Duration& duration) -> double { return jewels::time::to_seconds<double>(duration); },
      "Convert to seconds (float).")
    .def(
      "to_ns", [](const Duration& duration) -> int64_t { return duration.count(); }, "Convert to nanoseconds (int).")
    .def(
      "__abs__",
      [](const Duration& duration) -> Duration { return chrono::abs(duration); },
      "Calculate absolute value.")
    .def(
      "__eq__",
      [](const Duration& self, nb::handle other) -> bool
      {
        try
        {
          const auto other_duration = nb::cast<Duration>(other);
          return self == other_duration;
        }
        catch (nb::cast_error&)
        {
          return false;
        }
      },
      nb::arg("other"),
      nb::sig("def __eq__(self: Duration, other: object) -> bool"))
    .def(nb::self + nb::self)  // NOLINT(misc-redundant-expression) Definition of an operation, not an expression.
    .def(nb::self - nb::self)  // NOLINT(misc-redundant-expression) Definition of an operation, not an expression.
    .def(nb::self > nb::self)  // NOLINT(misc-redundant-expression) Definition of an operation, not an expression.
    .def(nb::self >= nb::self) // NOLINT(misc-redundant-expression) Definition of an operation, not an expression.
    .def(nb::self < nb::self)  // NOLINT(misc-redundant-expression) Definition of an operation, not an expression.
    .def(nb::self <= nb::self) // NOLINT(misc-redundant-expression) Definition of an operation, not an expression.
    .def("__add__", [](const Duration& self, const SyncTime& other) -> SyncTime { return self + other; })
    .def("__neg__", [](const Duration& self) -> Duration { return -self; })
    .def("__mul__", nb::overload_cast<const Duration&, double>(&multiply<Duration, double>))
    .def("__mul__", nb::overload_cast<const Duration&, int64_t>(&multiply<Duration, int64_t>))
    .def("__truediv__", nb::overload_cast<const Duration&, int64_t>(&divide<int64_t>))
    .def("__truediv__", nb::overload_cast<const Duration&, double>(&divide<double>))
    .def(
      "__repr__",
      [](const Duration& duration) -> std::string { return fmt::format("Duration({}ns)", duration.count()); })
    .def("__getstate__", [](const Duration& self) -> int64_t { return self.count(); })
    .def("__setstate__", [](Duration& self, int64_t nanoseconds) { self = Duration(nanoseconds); });
}

NB_MODULE(nb_sync_time, mod)
{
  mod.doc() = "Binding for time (SyncTime/Duration)";

  bind_sync_time(mod); // NOLINT(cert-err33-c) False positive
  bind_duration(mod);  // NOLINT(cert-err33-c) False positive
}

} // namespace clockwork
