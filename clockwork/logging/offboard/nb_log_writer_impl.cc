// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/channel_type.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/message_encoding.hh"
#include "clockwork/logging/offboard/types.hh"
#include "clockwork/logging/offboard/writer.hh"
#include "clockwork/logging/schema_encoding.hh"
#include "jewels/memory/memory_resource.hh"

#include <fmt10/format.h>
#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/string.h>      // IWYU pragma: keep
#include <nanobind/stl/string_view.h> // IWYU pragma: keep
#include <wise_enum.h>

#include <cstdint>
#include <memory_resource>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

using clockwork_logging::ChannelType;
using clockwork_logging::LogTimestamp;
using clockwork_logging::MessageEncoding;
using clockwork_logging::SchemaEncoding;
using clockwork_logging::offboard::Writer;

NB_MODULE(nb_log_writer_impl, mod)
{
  nanobind::module_::import_("clockwork.logging.readers.nb_types");

  mod.doc() = "Log writer";

  nanobind::class_<Writer>(mod, "LogWriter")
    .def(
      "__init__",
      [](Writer* ptr) { new (ptr) Writer{jewels::memory::MemoryResource{std::pmr::new_delete_resource()}}; },
      "Constructor.")
    .def(
      "open",
      [](Writer& obj, const std::string& out_uri, const std::string& config)
      {
        if (const auto open_result = obj.open(out_uri, config); !open_result)
        {
          const auto err = fmt::format("Failed to open log at {}: {}", out_uri, open_result.error());
          throw std::runtime_error(err);
        }
      },
      nanobind::arg("out_uri"),
      nanobind::arg("config"),
      "Open the log.")
    .def(
      "close",
      [](Writer& obj)
      {
        if (const auto close_result = obj.close(); !close_result)
        {
          const auto err = fmt::format("Failed to close output log: {}", close_result.error());
          throw std::runtime_error(err);
        }
      },
      "Close the log.")
    .def(
      "create_channel",
      [](
        Writer& obj,
        const std::string& channel_name,
        const std::string& message_encoding,
        const std::string& channel_type,
        const std::string& schema_name,
        const std::string& schema_encoding,
        const nanobind::bytes& schema_definition)
      {
        const auto maybe_message_encoding = wise_enum::from_string<MessageEncoding>(message_encoding);
        const auto maybe_channel_type = wise_enum::from_string<ChannelType>(channel_type);
        const auto maybe_schema_encoding = wise_enum::from_string<SchemaEncoding>(schema_encoding);
        const auto channel_metadata = clockwork_logging::offboard::LoggedChannelMetadata{
          .channel_name = channel_name,
          .message_encoding = maybe_message_encoding ? *maybe_message_encoding : MessageEncoding::undefined,
          .channel_type = maybe_channel_type ? *maybe_channel_type : ChannelType::regular,
          .schema_name = schema_name,
          .schema_encoding = maybe_schema_encoding ? *maybe_schema_encoding : SchemaEncoding::undefined,
          .schema_definition =
            std::string_view(static_cast<const char*>(schema_definition.data()), schema_definition.size()),
        };
        if (const auto create_result = obj.create_channel(channel_metadata); !create_result)
        {
          const auto err = fmt::format("Failed to create channel {}: {}", channel_name, create_result.error());
          throw std::runtime_error(err);
        }
      },
      nanobind::arg("channel_name"),
      nanobind::arg("message_encoding"),
      nanobind::arg("channel_type"),
      nanobind::arg("schema_name"),
      nanobind::arg("schema_encoding"),
      nanobind::arg("schema_definition"),
      "Create a logged channel.")
    .def(
      "write",
      [](
        Writer& obj,
        const std::string& channel_name,
        uint32_t sequence_number,
        LogTimestamp log_time,
        LogTimestamp transmit_time,
        const nanobind::ndarray<nanobind::numpy, const uint8_t, nanobind::shape<-1>>& header,
        const nanobind::ndarray<nanobind::numpy, const uint8_t, nanobind::shape<-1>>& data)
      {
        const auto message = clockwork_logging::offboard::LoggedMessage{
          .channel_name = channel_name,
          .sequence_number = sequence_number,
          .log_time = log_time,
          .transmit_time = transmit_time,
          .header = std::as_bytes(std::span(header.data(), header.nbytes())),
          .data = std::as_bytes(std::span(data.data(), data.nbytes())),
        };
        if (const auto write_result = obj.write(message); !write_result)
        {
          const auto err = fmt::format("Failed to write output log: {}", write_result.error());
          throw std::runtime_error(err);
        }
      },
      nanobind::arg("channel_name"),
      nanobind::arg("sequence_number"),
      nanobind::arg("log_time"),
      nanobind::arg("transmit_time"),
      nanobind::arg("header"),
      nanobind::arg("data"),
      "Write a message to the log.");
}
