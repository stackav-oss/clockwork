// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/writers/tests/support/test_log_writer_config.hh"

#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/common/signal_metadata_config_clk_cc.hh"
#include "clockwork/logging/channel_type_clk_cc.hh"
#include "clockwork/logging/log_uuid.hh"
#include "clockwork/logging/message_encoding_clk_cc.hh"
#include "clockwork/logging/nolint_helper.hh"
#include "clockwork/logging/schema_encoding_clk_cc.hh"
#include "clockwork/logging/tests/support/test_message_clk_cc.hh"
#include "clockwork/logging/writers/persistent_log_entry.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/serialization/py/tests/support/simple_schema_v1_clk_cc.hh"
#include "clockwork/serialization/py/tests/support/simple_schema_v2_clk_cc.hh"
#include "clockwork/tools/metrics_channel_metadata/metrics_channel_metadata_config_clk_cc.hh"
#include "jewels/container/compare.hh"
#include "jewels/container/tap/var_array.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/memory/pmr_shared_ptr.hh"
#include "jewels/std/expected.hh"
#include "jewels/std/span.hh"
#include "jewels/uuid/uuid.hh"

#include <gsl/util>

#include <algorithm>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace clockwork_logging::tests
{

namespace
{

/// Number of pinion buffer slots
constexpr auto num_slots = 10U;

} // namespace

[[nodiscard]] std::shared_ptr<clockwork::Tappy<LogWriterConfig<>>> get_test_log_writer_config()
{
  auto config_ptr = std::make_shared<clockwork::Tappy<LogWriterConfig<>>>();
  auto& config = *config_ptr;

  using MessageType1 = clockwork::Tappy<TestMessage1>;
  auto& channel1 = config.get_underlying_channels().emplace_back();
  channel1.set_uuid(LogUuid::random_uuid());
  channel1.set_num_slots(num_slots);
  channel1.set_message_size(sizeof(MessageType1));
  channel1.get_underlying_channel_name().set_truncate("channel1");
  channel1.set_message_encoding(static_cast<MessageEncoding>(clockwork::LoggingTraits<MessageType1>::message_encoding));
  channel1.get_underlying_schema_name().set_truncate(clockwork::LoggingTraits<MessageType1>::schema_name);
  channel1.set_schema_encoding(static_cast<SchemaEncoding>(clockwork::LoggingTraits<MessageType1>::schema_encoding));
  const auto schema_definition1 = std::as_bytes(std::span{clockwork::LoggingTraits<MessageType1>::schema_definition});
  channel1.get_underlying_schema_definition().insert(
    channel1.get_underlying_schema_definition().begin(), schema_definition1.begin(), schema_definition1.end());
  channel1.set_channel_type(ChannelType::regular);

  using MessageType2 = clockwork::Tappy<TestMessage2>;
  auto& channel2 = config.get_underlying_channels().emplace_back();
  channel2.set_uuid(LogUuid::random_uuid());
  channel2.set_num_slots(num_slots);
  channel2.set_message_size(sizeof(MessageType2));
  channel2.get_underlying_channel_name().set_truncate("channel2");
  channel2.set_message_encoding(static_cast<MessageEncoding>(clockwork::LoggingTraits<MessageType2>::message_encoding));
  channel2.get_underlying_schema_name().set_truncate(clockwork::LoggingTraits<MessageType2>::schema_name);
  channel2.set_schema_encoding(static_cast<SchemaEncoding>(clockwork::LoggingTraits<MessageType2>::schema_encoding));
  const auto schema_definition2 = std::as_bytes(std::span{clockwork::LoggingTraits<MessageType2>::schema_definition});
  channel2.get_underlying_schema_definition().insert(
    channel2.get_underlying_schema_definition().begin(), schema_definition2.begin(), schema_definition2.end());
  channel2.set_channel_type(ChannelType::persistent);

  auto& channel3 = config.get_underlying_channels().emplace_back();
  channel3.set_uuid(LogUuid::random_uuid());
  channel3.set_num_slots(num_slots);
  channel3.set_message_size(sizeof(MessageType2));
  channel3.get_underlying_channel_name().set_truncate("channel2");
  channel3.set_message_encoding(static_cast<MessageEncoding>(clockwork::LoggingTraits<MessageType2>::message_encoding));
  channel3.get_underlying_schema_name().set_truncate(clockwork::LoggingTraits<MessageType2>::schema_name);
  channel3.set_schema_encoding(static_cast<SchemaEncoding>(clockwork::LoggingTraits<MessageType2>::schema_encoding));
  channel3.get_underlying_schema_definition().insert(
    channel3.get_underlying_schema_definition().begin(), schema_definition2.begin(), schema_definition2.end());
  channel3.set_channel_type(ChannelType::persistent);

  return config_ptr;
}

[[nodiscard]] std::shared_ptr<clockwork::Tappy<LoggerConfig>>
get_test_logger_config(std::string_view log_root_dir, std::string_view pinion_shm_root)
{
  auto config_ptr = std::make_shared<clockwork::Tappy<LoggerConfig>>();
  auto& config = *config_ptr;
  config.get_underlying_log_root_dir().set_truncate(log_root_dir);
  config.get_underlying_pinion_shm_root().set_truncate(pinion_shm_root);
  config.get_underlying_pinion_namespace().set_truncate(LogUuid::random_uuid().to_string());
  config.set_max_write_mib_per_sec(100U);
  config.set_max_log_file_duration_sec(0);
  return config_ptr;
}

[[nodiscard]] std::shared_ptr<clockwork::Tappy<ChannelMessageRatesConfig>> get_test_channel_message_rates_config()
{
  auto config_ptr = std::make_shared<clockwork::Tappy<ChannelMessageRatesConfig>>();
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

[[nodiscard]] std::shared_ptr<clockwork::Tappy<ChannelPublisherConfig<>>> get_test_channel_publisher_config()
{
  auto config_ptr = std::make_shared<clockwork::Tappy<ChannelPublisherConfig<>>>();
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

[[nodiscard]] std::shared_ptr<const clockwork::Tappy<clockwork::tools::MetricsChannelMetadataConfig<>>>
get_test_metrics_channel_metadata_config()
{
  auto config_ptr = std::make_shared<clockwork::Tappy<clockwork::tools::MetricsChannelMetadataConfig<>>>();
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

  config.get_underlying_metrics_metadata_report_schema_name().set_truncate(
    clockwork::LoggingTraits<clockwork::Tappy<clockwork::tools::MetricsChannelMetadataReport<>>>::schema_name);
  const auto metrics_metadata_report_schema_definition = std::as_bytes(
    std::span{
      clockwork::LoggingTraits<clockwork::Tappy<clockwork::tools::MetricsChannelMetadataReport<>>>::schema_definition});
  config.get_underlying_metrics_metadata_report_schema_definition().resize(
    metrics_metadata_report_schema_definition.size());
  std::ranges::copy(
    metrics_metadata_report_schema_definition, config.get_mutable_metrics_metadata_report_schema_definition().begin());

  return config_ptr;
}

[[nodiscard]] std::shared_ptr<const clockwork::Tappy<clockwork::common::SignalMetadataConfig<>>>
get_test_signal_metadata_config()
{
  auto config_ptr = std::make_shared<clockwork::Tappy<clockwork::common::SignalMetadataConfig<>>>();
  auto& config = *config_ptr;

  auto& signal1 = config.get_underlying_signals().emplace_back();
  signal1.get_underlying_name().set_truncate("test_signal_1");

  auto& signal2 = config.get_underlying_signals().emplace_back();
  signal2.get_underlying_name().set_truncate("test_signal_2");

  return config_ptr;
}

[[nodiscard]] std::pmr::vector<PersistentLogEntry>
get_test_persistent_entries(jewels::memory::MemoryResource memres, bool include_metrics, bool include_signal_metadata)
{
  std::pmr::vector<PersistentLogEntry> entries{memres};

  if (include_metrics)
  {
    auto metrics_config = get_test_metrics_channel_metadata_config();
    auto report =
      jewels::memory::make_pmr_shared<clockwork::Tappy<clockwork::tools::MetricsChannelMetadataReport<>>>(memres);
    report->get_underlying_metrics_channels() = metrics_config->get_underlying_metrics_channels();

    entries.push_back(
      PersistentLogEntry{
        .channel_name = std::string(metrics_channel_metadata_channel_name),
        .schema_name = std::string(metrics_config->get_metrics_metadata_report_schema_name()),
        .schema_encoding = SchemaEncoding::clockwork_tachyon,
        .schema_definition = std::string(
          clockwork_logging::nolint_helper::byte_span_to_string_view(
            metrics_config->get_metrics_metadata_report_schema_definition())),
        .data_owner = report,
        .data = std::as_bytes(jewels::as_single_item_span(*report)),
      });
  }

  if (include_signal_metadata)
  {
    auto signal_config = get_test_signal_metadata_config();
    using SignalMetadataTappy = clockwork::Tappy<clockwork::common::SignalMetadataConfig<>>;
    using Traits = clockwork::LoggingTraits<SignalMetadataTappy>;
    auto schema_def_bytes = std::as_bytes(std::span{Traits::schema_definition});

    entries.push_back(
      PersistentLogEntry{
        .channel_name = std::string(signal_metadata_channel_name),
        .schema_name = std::string(Traits::schema_name),
        .schema_encoding = static_cast<SchemaEncoding>(Traits::schema_encoding),
        .schema_definition = std::string(clockwork_logging::nolint_helper::byte_span_to_string_view(schema_def_bytes)),
        .data_owner = signal_config,
        .data = std::as_bytes(jewels::as_single_item_span(*signal_config)),
      });
  }

  return entries;
}

} // namespace clockwork_logging::tests
