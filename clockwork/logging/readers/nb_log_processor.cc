// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/readers/log_processor.hh"
#include "clockwork/logging/readers/types.hh"
#include "jewels/std/expected.hh"

#include <Python.h>
#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>         // IWYU pragma: keep
#include <nanobind/stl/function.h>    // IWYU pragma: keep
#include <nanobind/stl/optional.h>    // IWYU pragma: keep
#include <nanobind/stl/string.h>      // IWYU pragma: keep
#include <nanobind/stl/string_view.h> // IWYU pragma: keep
#include <nanobind/stl/vector.h>      // IWYU pragma: keep

#include <functional>
#include <optional>
#include <string>
#include <vector>

NB_MODULE(nb_log_processor, mod)
{
  nanobind::module_::import_("clockwork.logging.readers.nb_types");

  nanobind::set_leak_warnings(false);

  mod.doc() = "Clockwork log processor python wrapper";

  // LogProcessor bindings
  nanobind::class_<clockwork_logging::LogProcessor>(mod, "LogProcessor")
    .def(nanobind::init<const clockwork_logging::LogReaderConfig&>(), nanobind::arg("config"), "Constructor.")
    .def(
      "add_raw_msg_callback",
      [](
        clockwork_logging::LogProcessor& obj,
        const std::string& topic,
        const std::function<void(const clockwork_logging::LoggedMessage&)>& callback)
      {
        // NOLINTNEXTLINE(cert-err33-c) False positive
        obj.add_raw_msg_callback(
          topic,
          [callback](const clockwork_logging::LoggedMessage& msg)
          {
            auto gil_acquire = nanobind::gil_scoped_acquire();
            callback(msg);
          });
      },
      nanobind::arg("topic"),
      nanobind::arg("callback"),
      "Add raw message callback.")
    .def(
      "try_get_topic_metadata",
      [](clockwork_logging::LogProcessor& obj, const std::string& topic)
        -> std::optional<clockwork_logging::TopicMetadata>
      {
        const auto metadata_result = obj.try_get_topic_metadata(topic);
        if (!metadata_result)
        {
          return {};
        }
        return metadata_result.value();
      },
      nanobind::arg("topic"),
      "Get topic metadata.")
    .def(
      "try_get_metrics",
      [](clockwork_logging::LogProcessor& obj) -> std::optional<clockwork_logging::LogMetrics>
      {
        const auto metrics_result = obj.get_metrics();
        if (!metrics_result)
        {
          return {};
        }
        return metrics_result.value();
      },
      "Get log metrics.")
    .def(
      "process",
      [](clockwork_logging::LogProcessor& obj) -> bool
      {
        auto release_gil = nanobind::gil_scoped_release();
        return obj.process();
      },
      "Process the log.")
    .def("next", &clockwork_logging::LogProcessor::next, "Advance to the next message.")
    .def("abort", &clockwork_logging::LogProcessor::abort, "Abort log processing.")
    .def(
      "start_time",
      [](clockwork_logging::LogProcessor& obj) -> std::optional<clockwork_logging::LogTimestamp>
      {
        const auto start_time_result = obj.start_time();
        if (!start_time_result)
        {
          return {};
        }
        return start_time_result.value();
      },
      "Log start time.")
    .def(
      "end_time",
      [](clockwork_logging::LogProcessor& obj) -> std::optional<clockwork_logging::LogTimestamp>
      {
        const auto end_time_result = obj.end_time();
        if (!end_time_result)
        {
          return {};
        }
        return end_time_result.value();
      },
      "Log end time.")
    .def_prop_ro("topics", [](clockwork_logging::LogProcessor& obj) { return obj.topics(); }, "Logged topics.");
}
