// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/offboard/chunk_reader_writer_factory.hh"
#include "clockwork/logging/readers/log_reader.hh"
#include "clockwork/logging/readers/types.hh"
#include "jewels/log_cerr/log_cerr.hh" // IWYU pragma: keep
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/span.hh"

#include <fmt/format.h>
#include <fmt/ostream.h>
#include <nanobind/make_iterator.h>
#include <nanobind/nanobind.h>
#include <nanobind/stl/function.h>    // IWYU pragma: keep
#include <nanobind/stl/optional.h>    // IWYU pragma: keep
#include <nanobind/stl/string.h>      // IWYU pragma: keep
#include <nanobind/stl/string_view.h> // IWYU pragma: keep
#include <nanobind/stl/vector.h>      // IWYU pragma: keep

#include <cstdint>
#include <functional>
#include <memory_resource>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

NB_MODULE(nb_log_reader, mod)
{
  nanobind::module_::import_("clockwork.logging.readers.nb_types");

  nanobind::set_leak_warnings(false);

  mod.doc() = "Log reader python wrapper";

  mod.def(
    "log_file_exists",
    [](std::string_view uri)
    {
      const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
      clockwork_logging::offboard::ChunkReaderWriterFactory factory{memory_resource};
      const auto result = factory.exists(uri);
      if (!result)
      {
        throw std::runtime_error(fmt::format("Failed to check {}: {}", uri, fmt::streamed(result.error())));
      }
      return result.value();
    },
    nanobind::arg("uri"),
    "Return whether a local or S3 log file exists.");

  mod.def(
    "read_log_file",
    [](std::string_view uri)
    {
      const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
      clockwork_logging::offboard::ChunkReaderWriterFactory factory{memory_resource};
      auto result = factory.read_log_file(uri);
      if (!result)
      {
        throw std::runtime_error(fmt::format("Failed to read {}: {}", uri, fmt::streamed(result.error())));
      }
      const auto chars = jewels::as_chars(std::span{result.value()});
      return nanobind::bytes{chars.data(), chars.size()};
    },
    nanobind::arg("uri"),
    "Read an opaque file from a local or S3 log URI.");

  // LogReader bindings
  nanobind::class_<clockwork_logging::LogReader>(mod, "LogReader")
    .def(
      "__init__",
      [](
        clockwork_logging::LogReader* ptr,
        std::string_view log_uri,
        std::optional<clockwork_logging::LogInterval> maybe_log_interval,
        std::optional<clockwork_logging::RelativeInterval> maybe_relative_interval)
      { new (ptr) clockwork_logging::LogReader{log_uri, maybe_log_interval, maybe_relative_interval}; },
      nanobind::arg("log_uri"),
      nanobind::arg("maybe_log_interval").none() = nanobind::none(),
      nanobind::arg("maybe_relative_interval").none() = nanobind::none(),
      "Constructor.")
    .def(
      "raw_messages",
      [](
        clockwork_logging::LogReader& obj,
        const std::optional<std::function<bool(std::string_view)>>& topic_filter,
        const std::optional<std::function<bool(std::string_view, uint32_t)>>& sequence_number_filter)
      {
        if (const auto open_result = obj.open(
              topic_filter.value_or([](std::string_view) { return true; }),
              sequence_number_filter.value_or(std::function<bool(std::string_view, uint32_t)>{}));
            !open_result)
        {
          throw std::runtime_error(fmt::format("Failed to open log: ", open_result.error()));
        }
        return nanobind::make_iterator(
          nanobind::type<clockwork_logging::LogReader>(), "message_iterator", obj.begin(), obj.end());
      },
      nanobind::arg("topic_filter").none() = nanobind::none(),
      nanobind::arg("sequence_number_filter").none() = nanobind::none(),
      nanobind::keep_alive<0, 1>(),
      "Logged message iterator.")
    .def(
      "try_get_topic_metadata",
      [](clockwork_logging::LogReader& obj, const std::string& topic) -> std::optional<clockwork_logging::TopicMetadata>
      {
        const auto metadata_result = obj.get_channel_metadata(topic);
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
      [](clockwork_logging::LogReader& obj) -> std::optional<clockwork_logging::LogMetrics>
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
      "start_time",
      [](clockwork_logging::LogReader& obj) -> std::optional<clockwork_logging::LogTimestamp>
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
      [](clockwork_logging::LogReader& obj) -> std::optional<clockwork_logging::LogTimestamp>
      {
        const auto end_time_result = obj.end_time();
        if (!end_time_result)
        {
          return {};
        }
        return end_time_result.value();
      },
      "Log end time.")
    .def_prop_ro(
      "metadata", [](clockwork_logging::LogReader& obj) { return obj.get_metadata(); }, "Logged topic metadata.");
}
