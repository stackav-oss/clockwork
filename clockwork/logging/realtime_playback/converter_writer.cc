// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/realtime_playback/converter_writer.hh"

#include "clockwork/logging/onboard/types.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/time/sync_time.hh"

#include <chrono>
#include <exception>
#include <memory>
#include <memory_resource>
#include <string>
#include <string_view>
#include <utility>

namespace clockwork_logging::realtime_playback
{

namespace
{

/// Maximum converter writer throughput in MiB per second.
constexpr size_t converter_max_write_mib_per_sec = 1024U;

} // namespace

ConverterMessageWriter::ConverterMessageWriter(const jewels::memory::MemoryResource memory_resource)
  : compressor_{memory_resource}
{
}

jewels::BinaryOutcome upgrade_converter_message(
  jewels::Out<std::vector<std::byte>> upgraded_out,
  const clockwork::serialization::TachyonUpgrader& upgrader,
  const size_t target_message_size,
  const std::span<const std::byte> source_data,
  const std::string_view channel_name)
{
  try
  {
    upgraded_out->resize(target_message_size);
    upgrader.upgrade(source_data, *upgraded_out);
    return jewels::success;
  }
  catch (const std::exception& error)
  {
    jewels::log_cerr_error("Failed to upgrade converter message for {}: {}", channel_name, error.what());
    return jewels::failure;
  }
}

jewels::BinaryOutcome open_converter_writer(
  jewels::Out<std::unique_ptr<ConverterWriter>> writer_out,
  const std::string_view path,
  const std::string_view log_name)
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  auto writer = std::make_unique<ConverterWriter>(
    memory_resource,
    memory_resource,
    converter_max_write_mib_per_sec,
    std::chrono::nanoseconds{0},
    onboard::WriterEnvironment::simulation);
  if (!writer->open_log(std::string{path}, std::string{log_name}, jewels::time::SteadyClock::now()))
  {
    return jewels::failure;
  }
  *writer_out = std::move(writer);
  return jewels::success;
}

jewels::BinaryOutcome close_converter_writer(jewels::InOut<ConverterWriter> writer) noexcept
{
  const auto close_result = writer->close_log(jewels::time::SteadyClock::now());
  const auto drain_result = writer->drain_async_operations();
  return close_result && drain_result ? jewels::success : jewels::failure;
}

} // namespace clockwork_logging::realtime_playback
