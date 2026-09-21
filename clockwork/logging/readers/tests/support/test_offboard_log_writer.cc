// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/readers/tests/support/test_offboard_log_writer.hh"

#include "clockwork/logging/lite_compressor.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/offboard/chunk_writer.hh"
#include "clockwork/logging/offboard/tests/support/test_support.hh"
#include "clockwork/logging/offboard/writer.hh"
#include "clockwork/logging/onboard/tests/support/test_support.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"

#include <cstdint>
#include <memory_resource>

namespace clockwork_logging::tests
{

TestOffboardLogWriter::TestOffboardLogWriter()
  : header1_(default_header1_size),
    data1_(default_data1_size),
    header2_(default_header2_size),
    data2_(default_data2_size),
    header3_(default_header3_size),
    data3_(default_data3_size)
{
  const auto memory_resource = jewels::memory::MemoryResource{std::pmr::new_delete_resource()};
  LiteCompressor lite_compressor{memory_resource};
  onboard::tests::fill_with_random_bytes(header1_);
  onboard::tests::fill_with_random_bytes(data1_);
  onboard::tests::fill_with_random_bytes(header2_);
  onboard::tests::fill_with_random_bytes(data2_);
  compressed_data2_ = offboard::tests::lite_compress(data2_, lite_compressor);
  onboard::tests::fill_with_random_bytes(header3_);
  onboard::tests::fill_with_random_bytes(data3_);
}

[[nodiscard]] LogExpected<void>
TestOffboardLogWriter::write_clockwork_test_log(std::string_view log_dir, size_t message_count) const
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  offboard::Writer writer{memory_resource};
  if (const auto open_result = writer.open(log_dir); !open_result)
  {
    return open_result;
  }
  if (const auto create_result = writer.create_channel(clockwork_metadata1); !create_result)
  {
    return create_result;
  }
  if (const auto create_result = writer.create_channel(clockwork_metadata2); !create_result)
  {
    return create_result;
  }
  if (const auto create_result = writer.create_channel(clockwork_metadata3); !create_result)
  {
    return create_result;
  }
  auto current_time = start_time;
  for (uint32_t i = 0U; i < message_count; ++i)
  {
    if (const auto write_result = writer.write(
          offboard::LoggedMessage{
            .channel_name = channel_name1,
            .sequence_number = i * 4U,
            .log_time = LogTimestamp{current_time + std::chrono::nanoseconds(1)},
            .transmit_time = LogTimestamp{current_time},
            .header = header1_,
            .data = data1_,
            .is_lite_compressed = false,
          });
        !write_result)
    {
      return write_result;
    }
    current_time += message_interval;

    if (const auto write_result = writer.write(
          offboard::LoggedMessage{
            .channel_name = channel_name2,
            .sequence_number = (i * 4U) + 1U,
            .log_time = LogTimestamp{current_time + std::chrono::nanoseconds(1)},
            .transmit_time = LogTimestamp{current_time},
            .header = header2_,
            .data = compressed_data2_,
            .is_lite_compressed = true,
          });
        !write_result)
    {
      return write_result;
    }
    current_time += message_interval;

    if (const auto write_result = writer.write(
          offboard::LoggedMessage{
            .channel_name = channel_name3,
            .sequence_number = (i * 4U) + 2U,
            .log_time = LogTimestamp{current_time + std::chrono::nanoseconds(1)},
            .transmit_time = LogTimestamp{current_time},
            .header = header3_,
            .data = data3_,
          });
        !write_result)
    {
      return write_result;
    }
    current_time += message_interval;
  }
  if (const auto close_result = writer.close(); !close_result)
  {
    return jewels::unexpected(close_result.error());
  }
  return {};
}

[[nodiscard]] std::span<const std::byte> TestOffboardLogWriter::header1() const
{
  return std::span{header1_};
}

void TestOffboardLogWriter::set_header1(std::span<const std::byte> header)
{
  header1_ = std::vector<std::byte>(header.begin(), header.end());
}

[[nodiscard]] std::span<const std::byte> TestOffboardLogWriter::data1() const
{
  return std::span{data1_};
}

void TestOffboardLogWriter::set_data1(std::span<const std::byte> data)
{
  data1_ = std::vector<std::byte>(data.begin(), data.end());
}

[[nodiscard]] std::span<const std::byte> TestOffboardLogWriter::header2() const
{
  return std::span{header2_};
}

void TestOffboardLogWriter::set_header2(std::span<const std::byte> header)
{
  header2_ = std::vector<std::byte>(header.begin(), header.end());
}

[[nodiscard]] std::span<const std::byte> TestOffboardLogWriter::data2() const
{
  return std::span{data2_};
}

[[nodiscard]] std::span<const std::byte> TestOffboardLogWriter::compressed_data2() const
{
  return std::span{compressed_data2_};
}

void TestOffboardLogWriter::set_data2(std::span<const std::byte> data)
{
  data2_ = std::vector<std::byte>(data.begin(), data.end());
}

[[nodiscard]] std::span<const std::byte> TestOffboardLogWriter::header3() const
{
  return std::span{header3_};
}

void TestOffboardLogWriter::set_header3(std::span<const std::byte> header)
{
  header3_ = std::vector<std::byte>(header.begin(), header.end());
}

[[nodiscard]] std::span<const std::byte> TestOffboardLogWriter::data3() const
{
  return std::span{data3_};
}

void TestOffboardLogWriter::set_data3(std::span<const std::byte> data)
{
  data3_ = std::vector<std::byte>(data.begin(), data.end());
}

} // namespace clockwork_logging::tests
