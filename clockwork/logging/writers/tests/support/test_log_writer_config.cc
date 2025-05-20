// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/writers/tests/support/test_log_writer_config.hh"

#include "clockwork/logging/channel_type.hh"
#include "clockwork/logging/log_uuid.hh"
#include "clockwork/logging/message_encoding.hh"
#include "clockwork/logging/schema_encoding.hh"
#include "jewels/container/tap/var_array.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/uuid/uuid.hh"

#include <span>
#include <string>
#include <string_view>

namespace clockwork_logging::tests
{

[[nodiscard]] std::unique_ptr<LogWriterConfigTap> get_test_log_writer_config()
{
  auto config_ptr = std::make_unique<LogWriterConfigTap>();
  auto& config = *config_ptr;
  auto& channel1 = config.get_underlying_channels().emplace_back();
  channel1.set_uuid(LogUuid::random_uuid());
  channel1.set_num_slots(10U);
  channel1.set_message_size(4096U);
  channel1.get_underlying_channel_name().set_truncate("channel1");
  channel1.set_message_encoding(MessageEncoding::tachyon);
  channel1.get_underlying_schema_name().set_truncate("TestType1");
  channel1.set_schema_encoding(SchemaEncoding::clockwork_tachyon);
  const auto schema_definition1 = std::as_bytes(std::span{std::string_view{"Schema definition 1"}});
  channel1.get_underlying_schema_definition().insert(
    channel1.get_underlying_schema_definition().begin(), schema_definition1.begin(), schema_definition1.end());
  channel1.set_channel_type(ChannelType::regular);
  auto& channel2 = config.get_underlying_channels().emplace_back();
  channel2.set_uuid(LogUuid::random_uuid());
  channel2.set_num_slots(10U);
  channel2.set_message_size(8192U);
  channel2.get_underlying_channel_name().set_truncate("channel2");
  channel2.set_message_encoding(MessageEncoding::tachyon);
  channel2.get_underlying_schema_name().set_truncate("TestType2");
  channel2.set_schema_encoding(SchemaEncoding::clockwork_tachyon);
  const auto schema_definition2 = std::as_bytes(std::span{std::string_view{"Schema definition 2"}});
  channel2.get_underlying_schema_definition().insert(
    channel2.get_underlying_schema_definition().begin(), schema_definition2.begin(), schema_definition2.end());
  channel2.set_channel_type(ChannelType::persistent);
  auto& channel3 = config.get_underlying_channels().emplace_back();
  channel3.set_uuid(LogUuid::random_uuid());
  channel3.set_num_slots(10U);
  channel3.set_message_size(8192U);
  channel3.get_underlying_channel_name().set_truncate("channel2");
  channel3.set_message_encoding(MessageEncoding::tachyon);
  channel3.get_underlying_schema_name().set_truncate("TestType2");
  channel3.set_schema_encoding(SchemaEncoding::clockwork_tachyon);
  channel3.get_underlying_schema_definition().insert(
    channel3.get_underlying_schema_definition().begin(), schema_definition2.begin(), schema_definition2.end());
  channel3.set_channel_type(ChannelType::persistent);
  return config_ptr;
}

[[nodiscard]] std::unique_ptr<LoggerConfigTap>
get_test_logger_config(std::string_view log_root_dir, std::string_view pinion_shm_root)
{
  auto config_ptr = std::make_unique<LoggerConfigTap>();
  auto& config = *config_ptr;
  config.get_underlying_log_root_dir().set_truncate(log_root_dir);
  config.get_underlying_pinion_shm_root().set_truncate(pinion_shm_root);
  config.get_underlying_pinion_namespace().set_truncate(LogUuid::random_uuid().to_string());
  config.set_max_write_mib_per_sec(100U);
  config.set_max_log_file_duration_sec(0);
  return config_ptr;
}

[[nodiscard]] std::unique_ptr<clockwork::Tappy<ChannelMessageRatesConfig>> get_test_channel_message_rates_config()
{
  auto config_ptr = std::make_unique<clockwork::Tappy<ChannelMessageRatesConfig>>();
  auto& config = *config_ptr;
  config.set_window_size_sec(5U);
  auto& channel1_rate = config.get_underlying_channel_message_rates().emplace_back();
  channel1_rate.get_underlying_channel_name().set_truncate("channel1");
  channel1_rate.set_min_msg_rate_hz(4.0);
  auto& channel2_rate = config.get_underlying_channel_message_rates().emplace_back();
  channel2_rate.get_underlying_channel_name().set_truncate("channel2");
  channel2_rate.set_min_msg_rate_hz(1.0);
  return config_ptr;
}

} // namespace clockwork_logging::tests
