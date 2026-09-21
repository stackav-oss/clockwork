// IWYU pragma: private, include "clockwork/logging/offboard/amendment_writer.hh"
#pragma once

#include "clockwork/logging/offboard/amendment_writer.hh"

#include "clockwork/logging/channel_type_clk_cc.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/offboard/chunk_writer.hh"
#include "clockwork/logging/offboard/log_format.hh"
#include "clockwork/logging/offboard/log_metadata_file_helper.hh"
#include "clockwork/logging/offboard/log_metadata_helper_interface.hh"
#include "clockwork/logging/offboard/log_uri.hh"
#include "clockwork/logging/offboard/types.hh"
#include "clockwork/logging/offboard/v1/log_amendment.pb.h"
#include "clockwork/logging/offboard/v1/log_metadata.pb.h"
#include "clockwork/logging/offboard/writer.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"

#include <fmt/base.h>

#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <memory_resource>
#include <string>
#include <string_view>
#include <utility>

namespace clockwork_logging::offboard
{

template <typename S3UtilsType>
AmendmentWriter<S3UtilsType>::AmendmentWriter(
  jewels::memory::MemoryResource memory_resource, MessageChunkIndexFormat message_chunk_index_format)
  : memory_resource_(std::move(memory_resource)),
    chunk_writer_factory_(memory_resource_),
    writer_(memory_resource_, message_chunk_index_format)
{
}

template <typename S3UtilsType>
LogOutcome AmendmentWriter<S3UtilsType>::open(
  std::string_view amendment_uri,
  std::string_view amended_path,
  std::string_view config_str,
  OverwriteMode overwrite_mode)
{
  if (writer_.is_open())
  {
    return LogError::already_open;
  }
  if (amendment_uri.ends_with("/"))
  {
    amendment_uri.remove_suffix(1U);
  }
  const auto amendment_uri_result = LogUri::try_make(amendment_uri, memory_resource_);
  if (!amendment_uri_result)
  {
    jewels::log_cerr_error("Invalid amendedment log URI: '{}'", amendment_uri);
    return LogError::invalid_log_uri;
  }
  LogUri amended_log_uri(memory_resource_);
  if (LogUri::is_absolute_path(amended_path))
  {
    const auto amended_uri_result = LogUri::try_make(amended_path, memory_resource_);
    if (!amended_uri_result)
    {
      jewels::log_cerr_error("Invalid amended log URI: '{}'", amended_path);
      return LogError::invalid_log_uri;
    }
    amended_log_uri = std::move(amended_uri_result).value();
  }
  else
  {
    amended_log_uri = amendment_uri_result->apply_relative_path(amended_path);
  }
  std::shared_ptr<LogMetadataHelperInterface> helper_ptr;
  if (const auto make_outcome =
        make_log_metadata_helper(memory_resource_, amended_log_uri, {}, false, chunk_writer_factory_, Out{helper_ptr});
      !ok(make_outcome))
  {
    jewels::log_cerr_error("Cannot access amended log: {}", make_outcome.get());
    return make_outcome;
  }
  if (const auto open_result = writer_.open(amendment_uri, config_str, overwrite_mode); !open_result)
  {
    return open_result.error();
  }
  amendment_uri_str_ = amendment_uri_result->string();
  amended_path_str_ = std::pmr::string{amended_path, memory_resource_};
  return LogError::success;
}

template <typename S3UtilsType>
LogOutcome AmendmentWriter<S3UtilsType>::close()
{
  ChunkWriter::WriteMetrics write_metrics{};
  ::clockwork::logging::offboard::v1::LogAmendment amendment_metadata;
  if (const auto close_result =
        writer_.close(Out{write_metrics}, Out{*amendment_metadata.mutable_amendment_metadata()});
      !ok(close_result))
  {
    return close_result;
  }
  if (LogUri::is_absolute_path(amended_path_str_))
  {
    amendment_metadata.mutable_amended_log_path()->set_absolute_path(amended_path_str_);
  }
  else
  {
    amendment_metadata.mutable_amended_log_path()->set_relative_path(amended_path_str_);
  }
  for (const auto& channel : amended_channels_)
  {
    amendment_metadata.mutable_amended_log_path()->add_excluded_channel(channel);
  }
  std::pmr::string amendment_metadata_uri{memory_resource_};
  fmt::format_to(std::back_inserter(amendment_metadata_uri), "{}/{}", amendment_uri_str_, log_amendment_filename);
  if (const auto write_result =
        chunk_writer_factory_.write_text_proto(amendment_metadata_uri, log_amendment_proto_header, amendment_metadata);
      !write_result)
  {
    return write_result.error();
  }
  return LogError::success;
}

template <typename S3UtilsType>
LogOutcome AmendmentWriter<S3UtilsType>::create_channel(const LoggedChannelMetadata& channel_metadata)
{
  auto metadata_copy = channel_metadata;
  metadata_copy.is_amended = true;
  if (const auto create_result = writer_.create_channel(metadata_copy); !create_result)
  {
    return create_result.error();
  }
  amended_channels_.insert(std::pmr::string{channel_metadata.channel_name, memory_resource_});
  return LogError::success;
}

template <typename S3UtilsType>
LogOutcome AmendmentWriter<S3UtilsType>::write(const LoggedMessage& message)
{
  if (const auto write_result = writer_.write(message); !write_result)
  {
    return write_result.error();
  }
  return LogError::success;
}

template <typename S3UtilsType>
LogOutcome AmendmentWriter<S3UtilsType>::write(const ZeroCopyLoggedMessage& message)
{
  if (const auto write_result = writer_.write(message); !write_result)
  {
    return write_result.error();
  }
  return LogError::success;
}

template <typename S3UtilsType>
template <clockwork::TappyType T>
LogOutcome AmendmentWriter<S3UtilsType>::create_channel(std::string_view channel_name, ChannelType channel_type)
{
  if (const auto create_result = writer_.template create_channel<T>(channel_name, channel_type, true); !create_result)
  {
    return create_result.error();
  }
  amended_channels_.insert(std::pmr::string{channel_name, memory_resource_});
  return LogError::success;
}

template <typename S3UtilsType>
template <clockwork::TappyType T>
LogOutcome AmendmentWriter<S3UtilsType>::write(
  std::string_view channel_name,
  uint32_t sequence_number,
  LogTimestamp log_time,
  LogTimestamp transmit_time,
  const T& message,
  bool is_repeated_persistent)
{
  if (const auto write_result =
        writer_.write(channel_name, sequence_number, log_time, transmit_time, message, is_repeated_persistent);
      !write_result)
  {
    return write_result.error();
  }
  return LogError::success;
}

} // namespace clockwork_logging::offboard
