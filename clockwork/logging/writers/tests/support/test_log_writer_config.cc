// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/writers/tests/support/test_log_writer_config.hh"

#include "clockwork/common/process_description.hh"
#include "clockwork/logging/channel_type.hh"
#include "clockwork/logging/log_uuid.hh"
#include "clockwork/serialization/py/tests/support/simple_schema_v1.hh"
#include "clockwork/serialization/py/tests/support/simple_schema_v2.hh"
#include "clockwork/tools/metrics_channel_metadata/metrics_channel_metadata_config.hh"
#include "jewels/container/compare.hh"
#include "jewels/container/tap/var_array.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"

#include <gsl/util>

#include <span>
#include <string>
#include <string_view>

namespace clockwork_logging::tests
{

namespace
{

/// Number of pinion buffer slots
constexpr auto num_slots = 10U;

} // namespace

[[nodiscard]] std::unique_ptr<LogWriterConfigTap> get_test_log_writer_config()
{
  auto config_ptr = std::make_unique<LogWriterConfigTap>();
  auto& config = *config_ptr;

  using MessageType1 = clockwork::Tappy<clockwork::tests::SimpleSchemaV1>;
  auto& channel1 = config.get_underlying_channels().emplace_back();
  channel1.set_uuid(LogUuid::random_uuid());
  channel1.set_num_slots(num_slots);
  channel1.set_message_size(sizeof(MessageType1));
  channel1.get_underlying_channel_name().set_truncate("channel1");
  channel1.set_message_encoding(clockwork::LoggingTraits<MessageType1>::message_encoding);
  channel1.get_underlying_schema_name().set_truncate(clockwork::LoggingTraits<MessageType1>::schema_name);
  channel1.set_schema_encoding(clockwork::LoggingTraits<MessageType1>::schema_encoding);
  const auto schema_definition1 = std::as_bytes(std::span{clockwork::LoggingTraits<MessageType1>::schema_definition});
  channel1.get_underlying_schema_definition().insert(
    channel1.get_underlying_schema_definition().begin(), schema_definition1.begin(), schema_definition1.end());
  channel1.set_channel_type(ChannelType::regular);

  using MessageType2 = clockwork::Tappy<clockwork::tests::SimpleSchemaV2>;
  auto& channel2 = config.get_underlying_channels().emplace_back();
  channel2.set_uuid(LogUuid::random_uuid());
  channel2.set_num_slots(num_slots);
  channel2.set_message_size(sizeof(MessageType2));
  channel2.get_underlying_channel_name().set_truncate("channel2");
  channel2.set_message_encoding(clockwork::LoggingTraits<MessageType2>::message_encoding);
  channel2.get_underlying_schema_name().set_truncate(clockwork::LoggingTraits<MessageType2>::schema_name);
  channel2.set_schema_encoding(clockwork::LoggingTraits<MessageType2>::schema_encoding);
  const auto schema_definition2 = std::as_bytes(std::span{clockwork::LoggingTraits<MessageType2>::schema_definition});
  channel2.get_underlying_schema_definition().insert(
    channel2.get_underlying_schema_definition().begin(), schema_definition2.begin(), schema_definition2.end());
  channel2.set_channel_type(ChannelType::persistent);

  auto& channel3 = config.get_underlying_channels().emplace_back();
  channel3.set_uuid(LogUuid::random_uuid());
  channel3.set_num_slots(num_slots);
  channel3.set_message_size(sizeof(MessageType2));
  channel3.get_underlying_channel_name().set_truncate("channel2");
  channel3.set_message_encoding(clockwork::LoggingTraits<MessageType2>::message_encoding);
  channel3.get_underlying_schema_name().set_truncate(clockwork::LoggingTraits<MessageType2>::schema_name);
  channel3.set_schema_encoding(clockwork::LoggingTraits<MessageType2>::schema_encoding);
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

[[nodiscard]] std::unique_ptr<ChannelPublisherConfigTap> get_test_channel_publisher_config()
{
  auto config_ptr = std::make_unique<ChannelPublisherConfigTap>();
  auto& config = *config_ptr;

  using MessageType1 = clockwork::Tappy<clockwork::tests::SimpleSchemaV1>;
  auto& channel1 = config.get_underlying_channels().emplace_back();
  channel1.set_uuid(LogUuid::random_uuid());
  channel1.set_num_slots(num_slots);
  channel1.set_message_size(sizeof(MessageType1));
  channel1.get_underlying_channel_name().set_truncate("channel1");
  const auto schema_definition1 = std::as_bytes(std::span{clockwork::LoggingTraits<MessageType1>::schema_definition});
  channel1.get_underlying_schema_definition().insert(
    channel1.get_underlying_schema_definition().begin(), schema_definition1.begin(), schema_definition1.end());
  channel1.get_underlying_module_name().set_truncate(clockwork::LoggingTraits<MessageType1>::module_name);
  channel1.get_underlying_source_file_name().set_truncate(clockwork::LoggingTraits<MessageType1>::source_file_name);
  channel1.get_underlying_class_name().set_truncate(clockwork::LoggingTraits<MessageType1>::class_name);

  using MessageType2 = clockwork::Tappy<clockwork::tests::SimpleSchemaV2>;
  auto& channel2 = config.get_underlying_channels().emplace_back();
  channel2.set_uuid(LogUuid::random_uuid());
  channel2.set_num_slots(num_slots);
  channel2.set_message_size(sizeof(MessageType2));
  channel2.get_underlying_channel_name().set_truncate("channel2");
  const auto schema_definition2 = std::as_bytes(std::span{clockwork::LoggingTraits<MessageType2>::schema_definition});
  channel2.get_underlying_schema_definition().insert(
    channel2.get_underlying_schema_definition().begin(), schema_definition2.begin(), schema_definition2.end());
  channel2.get_underlying_module_name().set_truncate(clockwork::LoggingTraits<MessageType2>::module_name);
  channel2.get_underlying_source_file_name().set_truncate(clockwork::LoggingTraits<MessageType2>::source_file_name);
  channel2.get_underlying_class_name().set_truncate(clockwork::LoggingTraits<MessageType2>::class_name);

  return config_ptr;
}

[[nodiscard]] std::shared_ptr<const clockwork::tools::MetricsChannelMetadataConfigTap>
get_test_metrics_channel_metadata_config()
{
  auto config_ptr = std::make_shared<clockwork::tools::MetricsChannelMetadataConfigTap>();
  auto& config = *config_ptr;
  auto& channel1 = config.get_underlying_metrics_channels().emplace_back();
  channel1.get_underlying_metrics_channel_name().set_truncate("metrics_channel1");
  auto channel1_uuid =
    jewels::Uuid<clockwork::common::EndpointInstanceId>::from_string("ad976474-9da7-4a5a-99fa-facef2e96287");
  channel1.set_metrics_channel_uuid(*channel1_uuid);
  channel1.get_underlying_cog_path().set_truncate("channel1::cog::path");
  channel1.get_underlying_cog_instance_path().set_truncate("channel1::cog::instance::path");

  auto& channel2 = config.get_underlying_metrics_channels().emplace_back();
  channel2.get_underlying_metrics_channel_name().set_truncate("metrics_channel2");
  auto channel_2_uuid =
    jewels::Uuid<clockwork::common::EndpointInstanceId>::from_string("9ce4091f-9889-48b5-9a3d-872711398340");
  channel2.set_metrics_channel_uuid(*channel_2_uuid);
  channel2.get_underlying_cog_path().set_truncate("channel2::cog::path");
  channel2.get_underlying_cog_instance_path().set_truncate("channel2::cog::instance::path");

  return config_ptr;
}

} // namespace clockwork_logging::tests
