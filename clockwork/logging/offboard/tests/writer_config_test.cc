// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/compression_type.hh"
#include "clockwork/logging/offboard/writer_config.hh"
#include "jewels/memory/memory_resource.hh"

#include <catch2/catch_test_macros.hpp>

#include <memory_resource>
#include <string>

namespace clockwork_logging::offboard
{
namespace
{

TEST_CASE("WriterConfig")
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};

  SECTION("Default config, all channels in other_channels")
  {
    const WriterConfig writer_config{memory_resource};

    REQUIRE(
      writer_config.get_channel_config("/foo/bar") == WriterConfig::ChannelConfig{
                                                        .compression_type = CompressionType::zstd,
                                                        .file_name_prefix = "other_channels",
                                                      });
    REQUIRE(
      writer_config.get_channel_config("foo_bar") == WriterConfig::ChannelConfig{
                                                       .compression_type = CompressionType::zstd,
                                                       .file_name_prefix = "other_channels",
                                                     });
  }

  SECTION("All channels in separate files")
  {
    WriterConfig writer_config{memory_resource};

    constexpr auto config_text = R"(
      # proto-file: clockwork/logging/offboard/v1/writer_config.proto
      # proto-message: WriterConfig
      rule {
        compression_type: COMPRESSION_TYPE_NONE
        regex: ".*"
      }
    )";

    REQUIRE(writer_config.set_config_proto(config_text));

    REQUIRE(
      writer_config.get_channel_config("/foo/bar") == WriterConfig::ChannelConfig{
                                                        .compression_type = CompressionType::none,
                                                        .file_name_prefix = "_foo_bar",
                                                      });
    REQUIRE(
      writer_config.get_channel_config("foo_bar") == WriterConfig::ChannelConfig{
                                                       .compression_type = CompressionType::none,
                                                       .file_name_prefix = "foo_bar",
                                                     });
  }

  SECTION("Some channels in separate files")
  {
    WriterConfig writer_config{memory_resource};

    constexpr auto config_text = R"(
      # proto-file: clockwork/logging/offboard/v1/writer_config.proto
      # proto-message: WriterConfig
      rule {
        compression_type: COMPRESSION_TYPE_NONE
        regex: ".*/uncompressed/.*"
      }
      rule {
        regex: ".*/group/.*"
        file_name_prefix: "group"
      }
      rule {
        regex: "/foo/bar/baz"
      }
      rule {
        regex: ".*"
        file_name_prefix: "not_group"
      }
    )";

    REQUIRE(writer_config.set_config_proto(config_text));

    REQUIRE(
      writer_config.get_channel_config("/foo/group/bar") == WriterConfig::ChannelConfig{
                                                              .compression_type = CompressionType::zstd,
                                                              .file_name_prefix = "group",
                                                            });
    REQUIRE(
      writer_config.get_channel_config("/foo/uncompressed/bar") == WriterConfig::ChannelConfig{
                                                                     .compression_type = CompressionType::none,
                                                                     .file_name_prefix = "_foo_uncompressed_bar",
                                                                   });
    REQUIRE(
      writer_config.get_channel_config("/foo/bar/baz") == WriterConfig::ChannelConfig{
                                                            .compression_type = CompressionType::zstd,
                                                            .file_name_prefix = "_foo_bar_baz",
                                                          });
    REQUIRE(
      writer_config.get_channel_config("/foo/other/bar") == WriterConfig::ChannelConfig{
                                                              .compression_type = CompressionType::zstd,
                                                              .file_name_prefix = "not_group",
                                                            });
  }
}

} // namespace
} // namespace clockwork_logging::offboard
