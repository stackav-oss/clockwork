// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/channel_type.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/message_encoding.hh"
#include "clockwork/logging/nolint_helper.hh"
#include "clockwork/logging/readers/types.hh"
#include "clockwork/logging/schema_encoding.hh"

#include <Python.h>
#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h> // IWYU pragma: keep
#include <nanobind/operators.h>
#include <nanobind/stl/function.h>    // IWYU pragma: keep
#include <nanobind/stl/optional.h>    // IWYU pragma: keep
#include <nanobind/stl/string.h>      // IWYU pragma: keep
#include <nanobind/stl/string_view.h> // IWYU pragma: keep
#include <nanobind/stl/vector.h>      // IWYU pragma: keep
#include <wise_enum.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/// Add the log timestamp bindings to the log reader types module
/// @param[in,out] mod Nanobind types module
void add_log_timestamp_bindings(auto& mod)
{
  nanobind::class_<clockwork_logging::LogTimestamp>(mod, "LogTimestamp")
    .def(nanobind::init<const int64_t>(), "Construct timestamp from nanoseconds since start of epoch.")
    .def_prop_ro(
      "nanoseconds",
      [](const clockwork_logging::LogTimestamp& obj) { return obj.get_nanoseconds(); },
      "Nanoseconds since start of epoch.")
    .def(nanobind::self == nanobind::self)  // NOLINT(misc-redundant-expression) Shorthand nanobind operator
    .def(nanobind::self != nanobind::self)  // NOLINT(misc-redundant-expression) Shorthand nanobind operator
    .def(nanobind::self < nanobind::self)   // NOLINT(misc-redundant-expression) Shorthand nanobind operator
    .def(nanobind::self > nanobind::self)   // NOLINT(misc-redundant-expression) Shorthand nanobind operator
    .def(nanobind::self <= nanobind::self)  // NOLINT(misc-redundant-expression) Shorthand nanobind operator
    .def(nanobind::self >= nanobind::self); // NOLINT(misc-redundant-expression) Shorthand nanobind operator
}

/// Add the log interval bindings to the log reader types module
/// @param[in,out] mod Nanobind types module
void add_log_interval_bindings(auto& mod)
{
  nanobind::class_<clockwork_logging::LogInterval>(mod, "LogInterval")
    .def(
      nanobind::init<const clockwork_logging::LogTimestamp>(),
      nanobind::arg("start_end_timestamp"),
      "Construct empty interval.")
    .def(
      nanobind::init<const clockwork_logging::LogTimestamp, const clockwork_logging::LogTimestamp>(),
      nanobind::arg("start_timestamp"),
      nanobind::arg("end_timestamp"),
      "Construct interval from start and end timestamp.")
    .def_prop_ro(
      "start_timestamp",
      [](const clockwork_logging::LogInterval& obj) { return obj.get_start_timestamp(); },
      "Interval start timestamp.")
    .def_prop_ro(
      "end_timestamp",
      [](const clockwork_logging::LogInterval& obj) { return obj.get_end_timestamp(); },
      "Interval end timestamp.")
    .def(nanobind::self == nanobind::self)  // NOLINT(misc-redundant-expression) Shorthand nanobind operator
    .def(nanobind::self != nanobind::self); // NOLINT(misc-redundant-expression) Shorthand nanobind operator
}

/// Add the relative interval bindings to the log reader types module
/// @param[in,out] mod Nanobind types module
void add_relative_interval_bindings(auto& mod)
{
  nanobind::class_<clockwork_logging::RelativeInterval>(mod, "RelativeInterval")
    .def(
      "__init__",
      [](
        clockwork_logging::RelativeInterval* ptr,
        std::optional<int64_t> start_offset,
        std::optional<int64_t> end_offset)
      {
        new (ptr) clockwork_logging::RelativeInterval{
          .start_offset = start_offset ? std::chrono::nanoseconds{*start_offset} : std::chrono::nanoseconds(0),
          .end_offset = end_offset ? std::chrono::nanoseconds{*end_offset}
                                   : std::chrono::nanoseconds(std::numeric_limits<int64_t>::max()),
        };
      },
      nanobind::arg("start_offset").none(),
      nanobind::arg("end_offset").none(),
      "Construct relative interval from optional start and end offsets.")
    .def_prop_ro(
      "start_offset",
      [](clockwork_logging::RelativeInterval& obj) { return obj.start_offset.count(); },
      "Interval start offset.")
    .def_prop_ro(
      "end_offset",
      [](clockwork_logging::RelativeInterval& obj) { return obj.end_offset.count(); },
      "Interval end offset.")
    .def(nanobind::self == nanobind::self)  // NOLINT(misc-redundant-expression) Shorthand nanobind operator
    .def(nanobind::self != nanobind::self); // NOLINT(misc-redundant-expression) Shorthand nanobind operator
}

/// Add the log reader config bindings to the log reader types module
/// @param[in,out] mod Nanobind types module
void add_log_reader_config_bindings(auto& mod)
{
  nanobind::class_<clockwork_logging::LogReaderConfig>(mod, "LogReaderConfig")
    .def(
      "__init__",
      [](clockwork_logging::LogReaderConfig* ptr, const std::string& uri)
      {
        new (ptr) clockwork_logging::LogReaderConfig{
          .uri = uri,
          .interval = std::nullopt,
          .relative_interval = std::nullopt,
          .topic_filter = {},
        };
      },
      nanobind::arg("uri"),
      "Construct from log URI.")
    .def(
      "__init__",
      [](
        clockwork_logging::LogReaderConfig* ptr,
        const std::string& uri,
        const std::optional<clockwork_logging::LogInterval>& interval)
      {
        new (ptr) clockwork_logging::LogReaderConfig{
          .uri = uri,
          .interval = interval,
          .relative_interval = std::nullopt,
          .topic_filter = {},
        };
      },
      nanobind::arg("uri"),
      nanobind::arg("interval").none(),
      "Construct from log URI and log interval.")
    .def(
      "__init__",
      [](
        clockwork_logging::LogReaderConfig* ptr,
        const std::string& uri,
        const std::optional<clockwork_logging::RelativeInterval>& relative_interval)
      {
        new (ptr) clockwork_logging::LogReaderConfig{
          .uri = uri,
          .interval = std::nullopt,
          .relative_interval = relative_interval,
          .topic_filter = {},
        };
      },
      nanobind::arg("uri"),
      nanobind::arg("relative_interval").none(),
      "Construct from log URI and relative log interval.")
    .def(
      "__init__",
      [](
        clockwork_logging::LogReaderConfig* ptr,
        const std::string& uri,
        const std::optional<clockwork_logging::LogInterval>& interval,
        const std::function<bool(std::string_view)>& topic_filter)
      {
        new (ptr) clockwork_logging::LogReaderConfig{
          .uri = uri,
          .interval = interval,
          .relative_interval = std::nullopt,
          .topic_filter = topic_filter,
        };
      },
      nanobind::arg("uri"),
      nanobind::arg("interval").none(),
      nanobind::arg("topic_filter"),
      "Construct from log URI, log interval and topic filter.")
    .def(
      "__init__",
      [](
        clockwork_logging::LogReaderConfig* ptr,
        const std::string& uri,
        const std::optional<clockwork_logging::RelativeInterval>& relative_interval,
        const std::function<bool(std::string_view)>& topic_filter)
      {
        new (ptr) clockwork_logging::LogReaderConfig{
          .uri = uri,
          .interval = std::nullopt,
          .relative_interval = relative_interval,
          .topic_filter = topic_filter,
        };
      },
      nanobind::arg("uri"),
      nanobind::arg("relative_interval").none(),
      nanobind::arg("topic_filter"),
      "Construct from log URI, relative log interval and topic filter.")
    .def_ro("uri", &clockwork_logging::LogReaderConfig::uri, "Log URI.")
    .def_ro("interval", &clockwork_logging::LogReaderConfig::interval, "Log interval.")
    .def_ro("relative_interval", &clockwork_logging::LogReaderConfig::relative_interval, "Relative log interval.")
    .def_ro("topic_filter", &clockwork_logging::LogReaderConfig::topic_filter, "Topic filter.");
}

/// Add the topic metadata bindings to the log reader types module
/// @param[in,out] mod Nanobind types module
void add_topic_metadata_bindings(auto& mod)
{
  nanobind::class_<clockwork_logging::TopicMetadata>(mod, "TopicMetadata")
    .def(
      "__init__",
      [](
        clockwork_logging::TopicMetadata* ptr,
        const std::string& name,
        const std::string& type,
        const std::string& message_encoding,
        const std::string& channel_type,
        const std::string& schema_encoding,
        const nanobind::bytes& schema_definition)
      {
        const auto maybe_message_encoding =
          wise_enum::from_string<clockwork_logging::MessageEncoding>(message_encoding);
        const auto maybe_channel_type = wise_enum::from_string<clockwork_logging::ChannelType>(channel_type);
        const auto maybe_schema_encoding = wise_enum::from_string<clockwork_logging::SchemaEncoding>(schema_encoding);
        new (ptr) clockwork_logging::TopicMetadata{
          .name = name,
          .type = type,
          .message_encoding =
            maybe_message_encoding ? *maybe_message_encoding : clockwork_logging::MessageEncoding::undefined,
          .channel_type = maybe_channel_type ? *maybe_channel_type : clockwork_logging::ChannelType::regular,
          .schema_encoding =
            maybe_schema_encoding ? *maybe_schema_encoding : clockwork_logging::SchemaEncoding::undefined,
          .schema_definition =
            std::string(static_cast<const char*>(schema_definition.data()), schema_definition.size()),
        };
      },
      nanobind::arg("name"),
      nanobind::arg("type"),
      nanobind::arg("message_encoding"),
      nanobind::arg("channel_type"),
      nanobind::arg("schema_encoding"),
      nanobind::arg("schema_definition"),
      "Constructor.")
    .def_ro("name", &clockwork_logging::TopicMetadata::name, "Topic name.")
    .def_ro("type", &clockwork_logging::TopicMetadata::type, "Schema name.")
    .def_prop_ro(
      "message_encoding",
      [](clockwork_logging::TopicMetadata& obj) { return wise_enum::to_string(obj.message_encoding); },
      "Message encoding.")
    .def_prop_ro(
      "channel_type",
      [](clockwork_logging::TopicMetadata& obj) { return wise_enum::to_string(obj.channel_type); },
      "Channel type.")
    .def_prop_ro(
      "schema_encoding",
      [](clockwork_logging::TopicMetadata& obj) { return wise_enum::to_string(obj.schema_encoding); },
      "Schema encoding.")
    .def_prop_ro(
      "schema_definition",
      [](clockwork_logging::TopicMetadata& obj)
      { return nanobind::bytes(obj.schema_definition.data(), obj.schema_definition.size()); },
      "Schema definition.")
    .def(nanobind::self == nanobind::self); // NOLINT(misc-redundant-expression) Shorthand nanobind operator
}

/// Add the logged message bindings to the log reader types module
/// @param[in,out] mod Nanobind types module
void add_logged_message_bindings(auto& mod)
{
  nanobind::class_<clockwork_logging::LoggedMessage>(mod, "LoggedMessage")
    .def(
      "__init__",
      [](
        clockwork_logging::LoggedMessage* ptr,
        std::string_view topic,
        uint32_t sequence_number,
        clockwork_logging::LogTimestamp publish_time,
        clockwork_logging::LogTimestamp log_time,
        const nanobind::bytes& header,
        const nanobind::bytes& data,
        bool is_repeated_persistent,
        std::string_view message_encoding)
      {
        const auto maybe_message_encoding =
          wise_enum::from_string<clockwork_logging::MessageEncoding>(message_encoding);
        new (ptr) clockwork_logging::LoggedMessage{
          .topic = topic,
          .sequence_number = sequence_number,
          .publish_time = publish_time,
          .log_time = log_time,
          .header = std::as_bytes(std::span(header.c_str(), header.size())),
          .data = std::as_bytes(std::span(data.c_str(), data.size())),
          .is_repeated_persistent = is_repeated_persistent,
          .message_encoding =
            maybe_message_encoding ? *maybe_message_encoding : clockwork_logging::MessageEncoding::undefined,
        };
      },
      nanobind::arg("topic").sig("str"),
      nanobind::arg("sequence_number").sig("int"),
      nanobind::arg("publish_time").sig("LogTimestamp"),
      nanobind::arg("log_time").sig("LogTimestamp"),
      nanobind::arg("header").sig("bytes"),
      nanobind::arg("data").sig("bytes"),
      nanobind::arg("is_repeated_persistent").sig("bool"),
      nanobind::arg("message_encoding").sig("str"))
    .def_ro("topic", &clockwork_logging::LoggedMessage::topic, nanobind::sig("def topic(self) -> str"))
    .def_ro(
      "sequence_number",
      &clockwork_logging::LoggedMessage::sequence_number,
      nanobind::sig("def sequence_number(self) -> int"))
    .def_ro(
      "publish_time",
      &clockwork_logging::LoggedMessage::publish_time,
      nanobind::sig("def publish_time(self) -> LogTimestamp"))
    .def_ro("log_time", &clockwork_logging::LoggedMessage::log_time)
    .def_prop_ro(
      "message_encoding",
      [](const clockwork_logging::LoggedMessage& obj)
      { return std::string{wise_enum::to_string(obj.message_encoding)}; },
      nanobind::sig("def message_encoding(self) -> str"))
    .def_prop_ro(
      "header",
      [](const clockwork_logging::LoggedMessage& obj)
      {
        const auto header_span = clockwork_logging::nolint_helper::byte_span_to_value_span<char>(obj.header);
        return nanobind::bytes(header_span.data(), header_span.size());
      },
      "Message header copy.")
    .def_prop_ro(
      "header_view",
      [](const clockwork_logging::LoggedMessage& obj)
      {
        std::array shape{obj.header.size()};
        return nanobind::ndarray<nanobind::numpy, const uint8_t, nanobind::shape<-1>>(
          obj.header.data(), shape.size(), shape.data(), nanobind::handle());
      },
      nanobind::sig("def header_view(self) -> memoryview[int]"),
      "Message header view.")
    .def_prop_ro(
      "data",
      [](const clockwork_logging::LoggedMessage& obj)
      {
        const auto data_span = clockwork_logging::nolint_helper::byte_span_to_value_span<char>(obj.data);
        return nanobind::bytes(data_span.data(), data_span.size());
      },
      "Message data copy.")
    .def_prop_ro(
      "data_view",
      [](const clockwork_logging::LoggedMessage& obj)
      {
        std::array shape{obj.data.size()};
        return nanobind::ndarray<nanobind::numpy, const uint8_t, nanobind::shape<-1>>(
          obj.data.data(), shape.size(), shape.data(), nanobind::handle());
      },
      nanobind::sig("def data_view(self) -> memoryview[int]"),
      "Message data view.")
    .def_ro(
      "is_repeated_persistent",
      &clockwork_logging::LoggedMessage::is_repeated_persistent,
      "Is the message a persistent message repeated at the start of the log.")
    .def(nanobind::self == nanobind::self); // NOLINT(misc-redundant-expression) Shorthand nanobind operator
}

/// Add the logged topic bindings to the log reader types module
/// @param[in,out] mod Nanobind types module
void add_logged_topic_bindings(auto& mod)
{
  nanobind::class_<clockwork_logging::LoggedTopicMetrics>(mod, "LoggedTopicMetrics")
    .def(
      "__init__",
      [](
        clockwork_logging::LoggedTopicMetrics* ptr,
        std::string topic,
        clockwork_logging::LogInterval transmit_time_interval,
        uint64_t message_count,
        uint64_t byte_count)
      {
        new (ptr) clockwork_logging::LoggedTopicMetrics{
          .topic = std::move(topic),
          .transmit_time_interval = transmit_time_interval,
          .message_count = message_count,
          .byte_count = byte_count,
        };
      },
      "Constructor.")
    .def_ro("topic", &clockwork_logging::LoggedTopicMetrics::topic, "Channel name.")
    .def_ro(
      "transmit_time_interval",
      &clockwork_logging::LoggedTopicMetrics::transmit_time_interval,
      "Transmit time interval.")
    .def_ro("message_count", &clockwork_logging::LoggedTopicMetrics::message_count, "Message count.")
    .def_ro("byte_count", &clockwork_logging::LoggedTopicMetrics::byte_count, "Byte count.")
    .def(nanobind::self == nanobind::self); // NOLINT(misc-redundant-expression) Shorthand nanobind operator
}

/// Add the log metrics bindings to the log reader types module
/// @param[in,out] mod Nanobind types module
void add_log_metrics_bindings(auto& mod)
{

  // Log metrics bindings
  nanobind::class_<clockwork_logging::LogMetrics>(mod, "LogMetrics")
    .def(
      "__init__",
      [](
        clockwork_logging::LogMetrics* ptr,
        clockwork_logging::LogInterval transmit_time_interval,
        uint64_t message_count,
        uint64_t byte_count,
        std::vector<clockwork_logging::LoggedTopicMetrics> topic_metrics)
      {
        new (ptr) clockwork_logging::LogMetrics{
          .transmit_time_interval = transmit_time_interval,
          .message_count = message_count,
          .byte_count = byte_count,
          .topic_metrics = std::move(topic_metrics),
        };
      },
      nanobind::arg("transmit_time_interval"),
      nanobind::arg("message_count"),
      nanobind::arg("byte_count"),
      nanobind::arg("topic_metrics"),
      "Constructor.")
    .def_ro("transmit_time_interval", &clockwork_logging::LogMetrics::transmit_time_interval, "Transmit time interval.")
    .def_ro("message_count", &clockwork_logging::LogMetrics::message_count, "Message count.")
    .def_ro("byte_count", &clockwork_logging::LogMetrics::byte_count, "Byte count.")
    .def_ro(
      "topic_metrics",
      &clockwork_logging::LogMetrics::topic_metrics,
      "Metrics for each logged channel.")
    .def(nanobind::self == nanobind::self); // NOLINT(misc-redundant-expression) Shorthand nanobind operator
}

NB_MODULE(nb_types, mod)
{
  nanobind::set_leak_warnings(false);

  mod.doc() = "Log reader types python wrapper";

  add_log_timestamp_bindings(mod);
  add_log_interval_bindings(mod);
  add_relative_interval_bindings(mod);
  add_log_reader_config_bindings(mod);
  add_topic_metadata_bindings(mod);
  add_logged_message_bindings(mod);
  add_logged_topic_bindings(mod);
  add_log_metrics_bindings(mod);
}
