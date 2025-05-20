// IWYU pragma: private, include "clockwork/logging/offboard/chunk_reader_writer_factory.hh"
#pragma once

#include "clockwork/logging/offboard/chunk_reader_writer_factory.hh"

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/nolint_helper.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/std/expected.hh"

#include <google/protobuf/text_format.h>

#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace clockwork_logging::offboard
{

template <typename ProtobufType>
[[nodiscard]] LogExpected<void> ChunkReaderWriterFactory::write_text_proto(
  std::string_view uri_str, std::string_view header, const ProtobufType& protobuf)
{
  std::string header_str{header};
  std::string proto_str;
  if (!google::protobuf::TextFormat::PrintToString(protobuf, &proto_str))
  {
    jewels::log_cerr_error("Failed to serialize proto for {}", uri_str);
    return jewels::unexpected(LogError::invalid_protobuf_file);
  }
  header_str.append(proto_str);
  return write_log_file(uri_str, std::as_bytes(std::span{header_str}));
}

template <typename ProtobufType>
[[nodiscard]] LogExpected<ProtobufType>
ChunkReaderWriterFactory::read_text_proto(std::string_view uri_str, ProtobufReadMode read_mode)
{
  const auto& read_result = read_log_file(uri_str);
  if (!read_result)
  {
    return jewels::unexpected(read_result.error());
  }
  if (read_mode == ProtobufReadMode::fail_if_empty && read_result.value().empty())
  {
    return jewels::unexpected(LogError::empty_metadata_file);
  }
  const std::string proto_str{nolint_helper::byte_span_to_string_view(read_result.value())};
  ProtobufType protobuf;
  if (!google::protobuf::TextFormat::ParseFromString(proto_str, &protobuf))
  {
    jewels::log_cerr_error("Failed to parse proto from {}", uri_str);
    return jewels::unexpected(LogError::invalid_protobuf_file);
  }
  return {std::move(protobuf)};
}

template <typename ProtobufType>
[[nodiscard]] LogExpected<void>
ChunkReaderWriterFactory::write_binary_proto(std::string_view uri_str, const ProtobufType& protobuf)
{
  std::string proto_str;
  if (!protobuf.SerializeToString(&proto_str))
  {
    jewels::log_cerr_error("Failed to serialize proto for {}", uri_str);
    return jewels::unexpected(LogError::invalid_protobuf_file);
  }
  return write_log_file(uri_str, std::as_bytes(std::span{proto_str}));
}

template <typename ProtobufType>
[[nodiscard]] LogExpected<ProtobufType>
ChunkReaderWriterFactory::read_binary_proto(std::string_view uri_str, ProtobufReadMode read_mode)
{
  const auto& read_result = read_log_file(uri_str);
  if (!read_result)
  {
    return jewels::unexpected(read_result.error());
  }
  if (read_mode == ProtobufReadMode::fail_if_empty && read_result.value().empty())
  {
    return jewels::unexpected(LogError::empty_metadata_file);
  }
  const std::string proto_str{nolint_helper::byte_span_to_string_view(read_result.value())};
  ProtobufType protobuf;
  if (!protobuf.ParseFromString(proto_str))
  {
    jewels::log_cerr_error("Failed to parse proto from {}", uri_str);
    return jewels::unexpected(LogError::invalid_protobuf_file);
  }
  return {std::move(protobuf)};
}

} // namespace clockwork_logging::offboard
