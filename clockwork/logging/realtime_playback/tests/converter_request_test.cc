// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/realtime_playback/converter_request.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace clockwork_logging::realtime_playback
{
namespace
{

TEST_CASE("Converter request defaults to the full source interval")
{
  constexpr std::array args{
    std::string_view{"--source"},
    std::string_view{"source.olog"},
    std::string_view{"--generated-config"},
    std::string_view{"config.tachyon"},
    std::string_view{"--output"},
    std::string_view{"bundle"},
  };
  ConverterRequest request;
  REQUIRE(jewels::ok(parse_converter_request(jewels::Out{request}, args)));
  CHECK(request.source_uri == "source.olog");
  CHECK(request.generated_config_path == "config.tachyon");
  CHECK(request.output_path == "bundle");
  CHECK_FALSE(request.start_time_ns);
  CHECK_FALSE(request.end_time_ns);

  LogInterval interval;
  REQUIRE(jewels::ok(resolve_converter_interval(jewels::Out{interval}, request, LogTimestamp{10}, LogTimestamp{20})));
  REQUIRE(interval == LogInterval{LogTimestamp{10}, LogTimestamp{20}});
}

TEST_CASE("Converter request resolves an inclusive absolute interval")
{
  constexpr std::array args{
    std::string_view{"--source"},
    std::string_view{"source.olog"},
    std::string_view{"--generated-config"},
    std::string_view{"config.tachyon"},
    std::string_view{"--output"},
    std::string_view{"bundle"},
    std::string_view{"--start-time-ns"},
    std::string_view{"10"},
    std::string_view{"--end-time-ns"},
    std::string_view{"20"},
  };
  ConverterRequest request;
  REQUIRE(jewels::ok(parse_converter_request(jewels::Out{request}, args)));
  CHECK(request.start_time_ns == 10);
  CHECK(request.end_time_ns == 20);

  LogInterval interval;
  REQUIRE(jewels::ok(resolve_converter_interval(jewels::Out{interval}, request, LogTimestamp{10}, LogTimestamp{20})));
  REQUIRE(interval.contains(LogTimestamp{10}));
  REQUIRE(interval.contains(LogTimestamp{20}));
}

TEST_CASE("Converter request rejects invalid interval combinations")
{
  constexpr std::array partial_args{
    std::string_view{"--source"},
    std::string_view{"source.olog"},
    std::string_view{"--generated-config"},
    std::string_view{"config.tachyon"},
    std::string_view{"--output"},
    std::string_view{"bundle"},
    std::string_view{"--start-time-ns"},
    std::string_view{"10"},
  };
  ConverterRequest request;
  REQUIRE(jewels::fails(parse_converter_request(jewels::Out{request}, partial_args)));

  constexpr int64_t five_ns = 5;
  constexpr int64_t four_ns = 4;
  constexpr int64_t negative_one_ns = -1;

  request.start_time_ns = five_ns;
  request.end_time_ns = four_ns;
  LogInterval interval;
  REQUIRE(jewels::fails(resolve_converter_interval(jewels::Out{interval}, request, LogTimestamp{0}, LogTimestamp{10})));

  request.start_time_ns = negative_one_ns;
  request.end_time_ns = four_ns;
  REQUIRE(jewels::fails(resolve_converter_interval(jewels::Out{interval}, request, LogTimestamp{0}, LogTimestamp{10})));
}

TEST_CASE("Converter request rejects conflicting and malformed command-line arguments")
{
  ConverterRequest request;
  constexpr std::array duplicate_args{
    std::string_view{"--source"},
    std::string_view{"source.olog"},
    std::string_view{"--source"},
    std::string_view{"other.olog"},
    std::string_view{"--generated-config"},
    std::string_view{"config.tachyon"},
    std::string_view{"--output"},
    std::string_view{"bundle"},
  };
  REQUIRE(jewels::fails(parse_converter_request(jewels::Out{request}, duplicate_args)));

  constexpr std::array malformed_args{
    std::string_view{"--source"},
    std::string_view{"source.olog"},
    std::string_view{"--generated-config"},
    std::string_view{"config.tachyon"},
    std::string_view{"--output"},
    std::string_view{"bundle"},
    std::string_view{"--start-time-ns"},
    std::string_view{"not-a-number"},
    std::string_view{"--end-time-ns"},
    std::string_view{"20"},
  };
  REQUIRE(jewels::fails(parse_converter_request(jewels::Out{request}, malformed_args)));

  constexpr std::array missing_required_args{
    std::string_view{"--source"},
    std::string_view{"source.olog"},
    std::string_view{"--output"},
    std::string_view{"bundle"},
  };
  REQUIRE(jewels::fails(parse_converter_request(jewels::Out{request}, missing_required_args)));
}

} // namespace
} // namespace clockwork_logging::realtime_playback
