// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/channel_type.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/message_encoding.hh"
#include "clockwork/logging/nolint_helper.hh"
#include "clockwork/logging/readers/abstract_log_reader.hh"
#include "clockwork/logging/readers/log_reader_factory.hh"
#include "clockwork/logging/readers/types.hh"
#include "clockwork/logging/schema_encoding.hh"
#include "clockwork/logging/zstd_helper.hh"
#include "clockwork/serialization/metadata/tachyon_model.pb.h"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"

#include <fmt10/base.h>
#include <google/protobuf/text_format.h>
#include <tclap/CmdLine.h>
#include <tclap/SwitchArg.h>
#include <tclap/ValueArg.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace clockwork_logging
{

/// Get the metadata from the log
/// @param[in] log_uri Log URI
/// @return Metadata or LogError on failure
[[nodiscard]] LogExpected<std::vector<TopicMetadata>> load_metadata(std::string_view log_uri)
{
  try
  {
    auto reader_ptr = make_reader(log_uri, {}, {});
    return reader_ptr->get_metadata();
  }
  catch (const std::invalid_argument& exc)
  {
    jewels::log_cerr_error("{}", exc.what());
    return jewels::unexpected(LogError::failed_to_load_metrics);
  }
  __builtin_unreachable();
}

/// Print a string encoded schema definition
/// @param[in] schema_definition Schema definition string view
void print_string_schema_definition(std::string_view schema_definition)
{
  size_t offset = 0U;
  while (offset < schema_definition.size())
  {
    const auto next_offset = std::min(schema_definition.find('\n', offset), schema_definition.size());
    fmt::print("        {}\n", schema_definition.substr(offset, next_offset - offset));
    offset = next_offset + 1U;
  }
}

/// Print a clockwork schema definition
/// @param[in] schema_definition Schema definition string view
void print_clockwork_schema_definition(std::string_view schema_definition)
{
  clockwork::serialization::metadata::TachyonMetadata metadata{};
  if (!metadata.ParseFromString(std::string{schema_definition}))
  {
    fmt::print("        <FAILED TO DESERIALIZE PROTOBUF>\n");
    return;
  }
  std::string metadata_str;
  if (!google::protobuf::TextFormat::PrintToString(metadata, &metadata_str))
  {
    fmt::print("        <FAILED TO SERIALIZE PROTOBUF TO STRING>\n");
    return;
  }
  print_string_schema_definition(metadata_str);
}

/// Print a compressed clockwork schema definition
/// @param[in] schema_definition Schema definition string view
void print_compressed_clockwork_schema_definition(std::string_view schema_definition)
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const auto decompress_result =
    zstd_decompress(std::as_bytes(std::span{schema_definition.data(), schema_definition.size()}), memory_resource);
  if (!decompress_result)
  {
    fmt::print("        <FAILED TO DECOMPRESS PROTOBUF>\n");
    return;
  }
  print_clockwork_schema_definition(nolint_helper::byte_span_to_string_view(decompress_result.value()));
}

/// Print the log metadata
/// @param[in] log_metadata Log metadata
/// @param[in] verbose Set to true to print the schema definition
void print_metadata(const std::vector<TopicMetadata>& log_metadata, bool verbose)
{
  fmt::print("\n");
  for (const auto& metadata : log_metadata)
  {
    fmt::print("{}\n", metadata.name);
    fmt::print("    Type: {}\n", metadata.type);
    if (verbose)
    {
      fmt::print("    Message Encoding: {}\n", metadata.message_encoding);
      fmt::print("    Channel Type:     {}\n", metadata.channel_type);
      fmt::print("    Schema Encoding:  {}\n", metadata.schema_encoding);
      fmt::print("    Schema Definition:\n");
      switch (metadata.schema_encoding)
      {
      case SchemaEncoding::clockwork_tachyon:
        print_clockwork_schema_definition(metadata.schema_definition);
        break;
      case SchemaEncoding::clockwork_tachyon_zstd:
        print_compressed_clockwork_schema_definition(metadata.schema_definition);
        break;
      case SchemaEncoding::unspecified:
        fmt::print("        <UNSPECIFIED>\n");
        break;
      case SchemaEncoding::undefined:
        fmt::print("        <UNDEFINED>\n");
        break;
      case SchemaEncoding::ros2msg:
      case SchemaEncoding::ros2idl:
        print_string_schema_definition(metadata.schema_definition);
        break;
      }
      fmt::print("\n");
    }
  }
}

} // namespace clockwork_logging

int main(int32_t argc, char* argv[])
{
  try
  {
    TCLAP::CmdLine cmd("log metadata", ' ', "1.0", true);
    const TCLAP::ValueArg<std::string> log_uri_arg("l", "log-uri", "Log URI", true, "", "uri", cmd);
    const TCLAP::SwitchArg verbose_arg("v", "verbose", "Verbose output", cmd);

    cmd.parse(argc, argv);

    const auto& log_uri = log_uri_arg.getValue();
    const auto& verbose = verbose_arg.getValue();

    const auto metadata_result = clockwork_logging::load_metadata(log_uri);
    if (!metadata_result)
    {
      jewels::log_cerr_error("Failed to get log_metadata: {}", metadata_result.error());
      return 1;
    }
    clockwork_logging::print_metadata(metadata_result.value(), verbose);
    return 0;
  }
  catch (const std::exception& exc)
  {
    std::cerr << "Caught unexpected exception: " << exc.what() << '\n';
    return 1;
  }
}
