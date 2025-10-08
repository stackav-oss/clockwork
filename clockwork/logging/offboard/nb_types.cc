// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/channel_type.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/message_encoding.hh"
#include "clockwork/logging/nolint_helper.hh"
#include "clockwork/logging/offboard/types.hh"
#include "clockwork/logging/schema_encoding.hh"

#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>         // IWYU pragma: keep
#include <nanobind/stl/list.h>        // IWYU pragma: keep
#include <nanobind/stl/string.h>      // IWYU pragma: keep
#include <nanobind/stl/string_view.h> // IWYU pragma: keep
#include <wise_enum.h>

#include <array>
#include <cstdint>
#include <new>
#include <span>
#include <string>
#include <string_view>

/// Add logged channel metadata bindings to the module
/// @param[in,out] mod Nanobind types module
void add_logged_channel_metadata_bindings(auto& mod)
{
  nanobind::class_<clockwork_logging::offboard::LoggedChannelMetadata>(mod, "LoggedChannelMetadata")
    .def(
      "__init__",
      [](
        clockwork_logging::offboard::LoggedChannelMetadata* ptr,
        std::string_view channel_name,
        std::string_view message_encoding,
        std::string_view channel_type,
        std::string_view schema_name,
        std::string_view schema_encoding,
        const nanobind::bytes& schema_definition)
      {
        const auto maybe_message_encoding =
          wise_enum::from_string<clockwork_logging::MessageEncoding>(message_encoding);
        const auto maybe_channel_type = wise_enum::from_string<clockwork_logging::ChannelType>(channel_type);
        const auto maybe_schema_encoding = wise_enum::from_string<clockwork_logging::SchemaEncoding>(schema_encoding);
        new (ptr) clockwork_logging::offboard::LoggedChannelMetadata{
          .channel_name = channel_name,
          .message_encoding =
            maybe_message_encoding ? *maybe_message_encoding : clockwork_logging::MessageEncoding::undefined,
          .channel_type = maybe_channel_type ? *maybe_channel_type : clockwork_logging::ChannelType::regular,
          .schema_name = schema_name,
          .schema_encoding =
            maybe_schema_encoding ? *maybe_schema_encoding : clockwork_logging::SchemaEncoding::undefined,
          .schema_definition =
            std::string_view(static_cast<const char*>(schema_definition.data()), schema_definition.size()),
        };
      },
      nanobind::arg("channel_name"),
      nanobind::arg("message_encoding"),
      nanobind::arg("channel_type"),
      nanobind::arg("schema_name"),
      nanobind::arg("schema_encoding"),
      nanobind::arg("schema_definition"),
      "Constructor.")
    .def_ro("channel_name", &clockwork_logging::offboard::LoggedChannelMetadata::channel_name, "Channel name.")
    .def_prop_ro(
      "message_encoding",
      [](clockwork_logging::offboard::LoggedChannelMetadata& obj)
      { return wise_enum::to_string(obj.message_encoding); },
      "Message encoding.")
    .def_prop_ro(
      "channel_type",
      [](clockwork_logging::offboard::LoggedChannelMetadata& obj) { return wise_enum::to_string(obj.channel_type); },
      "Channel type.")
    .def_ro("schema_name", &clockwork_logging::offboard::LoggedChannelMetadata::schema_name, "Schema name.")
    .def_prop_ro(
      "schema_encoding",
      [](clockwork_logging::offboard::LoggedChannelMetadata& obj) { return wise_enum::to_string(obj.schema_encoding); },
      "Schema encoding.")
    .def_prop_ro(
      "schema_definition",
      [](clockwork_logging::offboard::LoggedChannelMetadata& obj)
      { return nanobind::bytes(obj.schema_definition.data(), obj.schema_definition.size()); },
      "Schema definition.");
}

/// Add logged message bindings to the log reader types module
/// @param[in,out] mod Nanobind types module
void add_logged_message_bindings(auto& mod)
{
  nanobind::class_<clockwork_logging::offboard::LoggedMessage>(mod, "LoggedMessage")
    .def(
      "__init__",
      [](
        clockwork_logging::offboard::LoggedMessage* ptr,
        std::string_view channel_name,
        uint32_t sequence_number,
        clockwork_logging::LogTimestamp log_time,
        clockwork_logging::LogTimestamp transmit_time,
        const nanobind::ndarray<nanobind::numpy, const uint8_t, nanobind::shape<-1>>& header,
        const nanobind::ndarray<nanobind::numpy, const uint8_t, nanobind::shape<-1>>& data,
        bool is_repeated_persistent,
        std::string_view message_encoding,
        bool is_lite_compressed)
      {
        const auto maybe_message_encoding =
          wise_enum::from_string<clockwork_logging::MessageEncoding>(message_encoding);
        new (ptr) clockwork_logging::offboard::LoggedMessage{
          .channel_name = channel_name,
          .sequence_number = sequence_number,
          .log_time = log_time,
          .transmit_time = transmit_time,
          .header = std::as_bytes(std::span(header.data(), header.nbytes())),
          .data = std::as_bytes(std::span(data.data(), data.nbytes())),
          .is_repeated_persistent = is_repeated_persistent,
          .message_encoding =
            maybe_message_encoding ? *maybe_message_encoding : clockwork_logging::MessageEncoding::undefined,
          .is_lite_compressed = is_lite_compressed,
        };
      },
      nanobind::arg("channel_name").sig("str"),
      nanobind::arg("sequence_number").sig("int"),
      nanobind::arg("transmit_time").sig("LogTimestamp"),
      nanobind::arg("log_time").sig("LogTimestamp"),
      nanobind::arg("header").sig("bytes"),
      nanobind::arg("data").sig("bytes"),
      nanobind::arg("is_repeated_persistent").sig("bool"),
      nanobind::arg("message_encoding").sig("str"),
      nanobind::arg("is_lite_compressed").sig("bool"))
    .def_ro(
      "channel_name",
      &clockwork_logging::offboard::LoggedMessage::channel_name,
      nanobind::sig("def channel_name(self) -> str"))
    .def_ro(
      "sequence_number",
      &clockwork_logging::offboard::LoggedMessage::sequence_number,
      nanobind::sig("def sequence_number(self) -> int"))
    .def_ro(
      "log_time",
      &clockwork_logging::offboard::LoggedMessage::log_time,
      nanobind::sig("def log_time(self) -> LogTimestamp"))
    .def_ro(
      "transmit_time",
      &clockwork_logging::offboard::LoggedMessage::transmit_time,
      nanobind::sig("def transmit_time(self) -> LogTimestamp"))
    .def_prop_ro(
      "header",
      [](const clockwork_logging::offboard::LoggedMessage& obj)
      {
        const auto header_span = clockwork_logging::nolint_helper::byte_span_to_value_span<char>(obj.header);
        return nanobind::bytes(header_span.data(), header_span.size());
      },
      "Message header copy.")
    .def_prop_ro(
      "header_view",
      [](const clockwork_logging::offboard::LoggedMessage& obj)
      {
        std::array shape{obj.header.size()};
        return nanobind::ndarray<nanobind::numpy, const uint8_t, nanobind::shape<-1>>(
          obj.header.data(), shape.size(), shape.data(), nanobind::handle());
      },
      nanobind::sig("def header_view(self) -> memoryview[int]"),
      "Message header view.")
    .def_prop_ro(
      "data",
      [](const clockwork_logging::offboard::LoggedMessage& obj)
      {
        const auto data_span = clockwork_logging::nolint_helper::byte_span_to_value_span<char>(obj.data);
        return nanobind::bytes(data_span.data(), data_span.size());
      },
      "Message data copy.")
    .def_prop_ro(
      "data_view",
      [](const clockwork_logging::offboard::LoggedMessage& obj)
      {
        std::array shape{obj.data.size()};
        return nanobind::ndarray<nanobind::numpy, const uint8_t, nanobind::shape<-1>>(
          obj.data.data(), shape.size(), shape.data(), nanobind::handle());
      },
      nanobind::sig("def data_view(self) -> memoryview[int]"),
      "Message data view.")
    .def_ro(
      "is_repeated_persistent",
      &clockwork_logging::offboard::LoggedMessage::is_repeated_persistent,
      "Is the message a persistent message repeated at the start of the log.")
    .def_prop_ro(
      "message_encoding",
      [](const clockwork_logging::offboard::LoggedMessage& obj)
      { return std::string{wise_enum::to_string(obj.message_encoding)}; },
      nanobind::sig("def message_encoding(self) -> str"))
    .def_ro(
      "is_lite_compressed",
      &clockwork_logging::offboard::LoggedMessage::is_lite_compressed,
      "Is the message lite compressed.");
}

NB_MODULE(nb_types, mod)
{
  nanobind::module_::import_("clockwork.logging.readers.nb_types");

  nanobind::set_leak_warnings(false);

  mod.doc() = "Offboard writer types";

  add_logged_channel_metadata_bindings(mod);
  add_logged_message_bindings(mod);
}
