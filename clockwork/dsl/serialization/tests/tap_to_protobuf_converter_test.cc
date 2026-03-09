// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/dsl/tests/support/better_than_inheritance.pb.h"
#include "clockwork/dsl/tests/support/better_than_inheritance_cpp.hh"
#include "clockwork/dsl/tests/support/convert_hello_msg.hh"
#include "clockwork/dsl/tests/support/msg_with_au.hh"
#include "clockwork/dsl/tests/support/msg_with_au_proto.pb.h"
#include "clockwork/dsl/tests/support/proto_tester_onboard.pb.h"
#include "jewels/container/tap/var_array.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <au/quantity.hh>
#include <au/units/meters.hh>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <google/protobuf/duration.pb.h>
#include <google/protobuf/timestamp.pb.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <span>
#include <string>
#include <vector>

namespace clockwork
{
TEST_CASE("Validate TAP to Protobuf Conversion")
{
  using namespace std::chrono_literals;
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};

  constexpr int64_t a_field = 43;
  constexpr int64_t my_optional_val = 23442334;
  auto my_uuid = ::jewels::Uuid<demo::BetterThanInheritance>::random_uuid();
  const std::string my_string_val = "This is a string";
  const std::vector<std::byte> my_bytes_val = {
    std::byte{'s'}, std::byte{'s'}, std::byte{'f'}, std::byte{'e'}, std::byte{'s'}, std::byte{'v'}, std::byte{'b'}};
  constexpr int16_t funny_number = 12; // Assuming FunnyNumber is int16 in schema
  constexpr jewels::time::SyncTime my_sync_time{3s};
  constexpr std::chrono::nanoseconds a_duration{1000ns};
  constexpr std::chrono::nanoseconds duration1{500ns};  // Duration for arrays
  constexpr std::chrono::nanoseconds duration2{1500ns}; // Duration for arrays

  constexpr int64_t seqno1 = 124253;
  constexpr int32_t other_number1 = 45;
  constexpr int64_t seqno2 = 245;
  constexpr int32_t other_number2 = 542;

  constexpr int64_t primitive_one = 13134;
  constexpr int64_t primitive_two = 413278;

  // --- Create and populate the TAP object ---
  Tap<Tachyon<demo::BetterThanInheritance>> tap_source;
  tap_source.set_a_field(a_field);
  tap_source.set_my_optional(my_optional_val); // Set optional primitive
  tap_source.set_my_uuid(my_uuid);

  tap_source.get_underlying_my_string().set_truncate(my_string_val);

  auto& bytes_array = tap_source.get_underlying_my_bytes();
  bytes_array.clear();
  for (auto the_byte : my_bytes_val)
  {
    bytes_array.push_back(the_byte);
  }

  tap_source.set_funny_number(funny_number);
  tap_source.set_sync_time(my_sync_time);
  tap_source.set_my_duration(a_duration);

  tap_source.set_opt_duration(a_duration);
  tap_source.set_opt_uuid(my_uuid);

  // Create a new ProtoTester and then set it as the opt_composition
  Tap<Tachyon<demo::ProtoTester>> proto_tester;
  proto_tester.set_seqno(seqno2);
  proto_tester.set_other_number(other_number2);
  tap_source.set_opt_composition(proto_tester);

  // VarArray fields
  auto& composition_var = tap_source.get_underlying_composition_var();
  composition_var.resize(2);
  composition_var[0].set_seqno(seqno1);
  composition_var[0].set_other_number(other_number1);
  composition_var[1].set_seqno(seqno2);
  composition_var[1].set_other_number(other_number2);

  auto& enum_arr = tap_source.get_underlying_enum_arr();
  enum_arr.resize(2);
  enum_arr[0] = demo::HelloEnum::hi;
  enum_arr[1] = demo::HelloEnum::ni_hao;

  // Add population for duration_ar
  auto& duration_ar = tap_source.get_underlying_duration_ar();
  duration_ar.resize(2);
  duration_ar[0] = duration1;
  duration_ar[1] = duration2;

  // For enum arrays, create a temporary array and then set it
  std::array<demo::HelloEnum, 2> enum_arr_data{demo::HelloEnum::hi, demo::HelloEnum::ni_hao};
  tap_source.set_fixed_enum_arr(enum_arr_data);

  // For duration arrays, create a temporary array and then set it
  std::array<std::chrono::nanoseconds, 2> duration_arr_data{duration1, duration2};
  tap_source.set_fixed_duration_ar(duration_arr_data);

  // For primitive arrays, create a temporary array and then set it
  std::array<int64_t, 2> primitive_arr_data{primitive_one, primitive_two};
  tap_source.set_fixed_primitive(primitive_arr_data);

  // For composition arrays, create temporary ProtoTesters
  Tap<Tachyon<demo::ProtoTester>> proto_tester1;
  proto_tester1.set_seqno(seqno1);
  proto_tester1.set_other_number(other_number1);

  Tap<Tachyon<demo::ProtoTester>> proto_tester2;
  proto_tester2.set_seqno(seqno2);
  proto_tester2.set_other_number(other_number2);

  std::array<Tap<Tachyon<demo::ProtoTester>>, 2> composition_arr_data{proto_tester1, proto_tester2};
  tap_source.set_fixed_composition(composition_arr_data);

  std::array<std::byte, 100> bytes_arr_data{};
  std::ranges::copy(my_bytes_val, bytes_arr_data.begin());

  tap_source.set_fixed_bytes(bytes_arr_data);

  // Nested generic schema - need to create and set properly
  auto generic1 = Tap<Tachyon<demo::GenericMsg<float, 32>>>{};
  generic1.set_my_hello(demo::HelloEnum::hola);
  tap_source.set_my_generic(generic1);

  auto generic2 = Tap<Tachyon<demo::GenericMsg<uint64_t, 16>>>{};
  generic2.set_my_hello(demo::HelloEnum::ni_hao);
  tap_source.set_another_generic(generic2);

  // --- Create the Protobuf destination object ---
  my_proto::BetterThanInheritance proto_destination;

  // --- Perform the conversion ---
  demo::tap_to_protobuf(proto_destination, tap_source);

  // Basic fields
  CHECK(proto_destination.a_field() == a_field);
  CHECK(proto_destination.my_optional() == my_optional_val);
  CHECK(proto_destination.my_uuid() == my_uuid.to_string());
  CHECK(proto_destination.my_string() == my_string_val);
  CHECK(
    proto_destination.my_bytes() ==
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) check to make sure underlying bytes are identical
    std::string(reinterpret_cast<const char*>(my_bytes_val.data()), my_bytes_val.size()));
  CHECK(proto_destination.funny_number() == funny_number);

  // Check Time/Duration
  CHECK(
    proto_destination.sync_time().seconds() ==
    std::chrono::duration_cast<std::chrono::seconds>(my_sync_time.time_since_epoch()).count());
  // Check nanos part of SyncTime
  CHECK(proto_destination.sync_time().nanos() == (my_sync_time.time_since_epoch() % 1s).count());
  CHECK(proto_destination.my_duration().seconds() == 0); // Assuming a_duration < 1s
  CHECK(proto_destination.my_duration().nanos() == a_duration.count());

  // Check Optional fields
  REQUIRE(proto_destination.has_opt_duration());
  CHECK(proto_destination.opt_duration().nanos() == a_duration.count());
  REQUIRE(proto_destination.has_opt_uuid());
  CHECK(proto_destination.opt_uuid() == my_uuid.to_string());
  REQUIRE(proto_destination.has_opt_composition());
  CHECK(proto_destination.opt_composition().seqno() == seqno2);
  CHECK(proto_destination.opt_composition().other_number() == other_number2);

  // Check VarArray fields
  REQUIRE(proto_destination.composition_var_size() == 2);
  CHECK(proto_destination.composition_var(0).seqno() == seqno1);
  CHECK(proto_destination.composition_var(0).other_number() == other_number1);
  CHECK(proto_destination.composition_var(1).seqno() == seqno2);
  CHECK(proto_destination.composition_var(1).other_number() == other_number2);

  REQUIRE(proto_destination.enum_arr_size() == 2);
  CHECK(proto_destination.enum_arr(0) == my_proto::HelloEnum::HELLO_ENUM_HI);
  CHECK(proto_destination.enum_arr(1) == my_proto::HelloEnum::HELLO_ENUM_NI_HAO);

  // Check duration array
  REQUIRE(proto_destination.duration_ar_size() == 2);
  CHECK(proto_destination.duration_ar(0).nanos() == duration1.count());
  CHECK(proto_destination.duration_ar(1).nanos() == duration2.count());

  // Check FixedArray fields
  REQUIRE(proto_destination.fixed_enum_arr_size() == 2);
  CHECK(proto_destination.fixed_enum_arr(0) == my_proto::HelloEnum::HELLO_ENUM_HI);
  CHECK(proto_destination.fixed_enum_arr(1) == my_proto::HelloEnum::HELLO_ENUM_NI_HAO);

  // Check fixed duration array
  REQUIRE(proto_destination.fixed_duration_ar_size() == 2);
  CHECK(proto_destination.fixed_duration_ar(0).nanos() == duration1.count());
  CHECK(proto_destination.fixed_duration_ar(1).nanos() == duration2.count());

  REQUIRE(proto_destination.fixed_primitive_size() == 2);
  CHECK(proto_destination.fixed_primitive(0) == primitive_one);
  CHECK(proto_destination.fixed_primitive(1) == primitive_two);

  REQUIRE(proto_destination.fixed_composition_size() == 2);
  CHECK(proto_destination.fixed_composition(0).seqno() == seqno1);
  CHECK(proto_destination.fixed_composition(0).other_number() == other_number1);
  CHECK(proto_destination.fixed_composition(1).seqno() == seqno2);
  CHECK(proto_destination.fixed_composition(1).other_number() == other_number2);

  // Check fixed_bytes content
  CHECK(
    proto_destination.fixed_bytes().substr(0, my_bytes_val.size()) ==
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) check to make sure underlying bytes are identical
    std::string(reinterpret_cast<const char*>(my_bytes_val.data()), my_bytes_val.size()));

  // Check nested generic schema
  REQUIRE(proto_destination.has_my_generic());
  CHECK(proto_destination.my_generic().my_hello() == my_proto::HelloEnum::HELLO_ENUM_HOLA);
  REQUIRE(proto_destination.has_another_generic());
  CHECK(proto_destination.another_generic().my_hello() == my_proto::HelloEnum::HELLO_ENUM_NI_HAO);

  SECTION("Test with optional fields not set")
  {
    // Test with optional fields not set
    Tap<Tachyon<demo::BetterThanInheritance>> tap_source_optional_missing;
    my_proto::BetterThanInheritance proto_destination_optional_missing;

    // Set only non-optional, non-array fields
    tap_source_optional_missing.set_a_field(a_field);
    tap_source_optional_missing.set_my_uuid(my_uuid);
    tap_source_optional_missing.get_underlying_my_string().set_truncate("");

    tap_source_optional_missing.set_funny_number(funny_number);
    tap_source_optional_missing.set_sync_time(my_sync_time);
    tap_source_optional_missing.set_my_duration(a_duration);

    demo::tap_to_protobuf(proto_destination_optional_missing, tap_source_optional_missing);

    CHECK_FALSE(proto_destination_optional_missing.has_my_optional());
    CHECK_FALSE(proto_destination_optional_missing.has_opt_duration());
    CHECK_FALSE(proto_destination_optional_missing.has_opt_uuid());
    CHECK_FALSE(proto_destination_optional_missing.has_opt_composition());

    // Check VarArrays are empty in proto
    CHECK(proto_destination_optional_missing.composition_var_size() == 0);
    CHECK(proto_destination_optional_missing.enum_arr_size() == 0);
    CHECK(proto_destination_optional_missing.duration_ar_size() == 0);
  }
}

TEST_CASE("Validate TAP to Protobuf conversion with strong type")
{
  Tap<Tachyon<testing::MsgWithAu>> tap_source;
  constexpr double distance_val = 22.678;

  tap_source.set_distance(au::meters(distance_val));

  proto_testing::MsgWithAu proto_destination;
  testing::tap_to_protobuf(proto_destination, tap_source);
  CHECK_THAT(proto_destination.distance(), Catch::Matchers::WithinAbs(distance_val, .001));

  Tap<Tachyon<testing::MsgWithMultipleAu>> tap_source_mulitple_au;
  constexpr double distance1_val = 11.123;
  constexpr double distance2_val = 44.567;

  tap_source_mulitple_au.set_distance(au::meters(distance1_val));
  tap_source_mulitple_au.set_distance2(au::meters(distance2_val));

  proto_testing::MsgWithMultipleAu proto_destination_multiple_au;
  testing::tap_to_protobuf(proto_destination_multiple_au, tap_source_mulitple_au);

  CHECK_THAT(proto_destination_multiple_au.distance(), Catch::Matchers::WithinAbs(distance1_val, .001));
  CHECK_THAT(proto_destination_multiple_au.distance2(), Catch::Matchers::WithinAbs(distance2_val, .001));
}

TEST_CASE("Validate TAP to Protobuf conversion with VarArray of strong type")
{
  Tap<Tachyon<testing::MsgWithMultipleAu>> tap_source_var_array_au;
  constexpr double distance_arr_val1 = 5.5;
  constexpr double distance_arr_val2 = 10.1;

  auto& distance_array_tap = tap_source_var_array_au.get_underlying_distance_array();
  distance_array_tap.resize(2);
  distance_array_tap[0] = au::meters(distance_arr_val1);
  distance_array_tap[1] = au::meters(distance_arr_val2);

  proto_testing::MsgWithMultipleAu proto_destination_var_array_au;
  testing::tap_to_protobuf(proto_destination_var_array_au, tap_source_var_array_au);

  REQUIRE(proto_destination_var_array_au.distance_array_size() == 2);
  CHECK_THAT(proto_destination_var_array_au.distance_array(0), Catch::Matchers::WithinAbs(distance_arr_val1, .001));
  CHECK_THAT(proto_destination_var_array_au.distance_array(1), Catch::Matchers::WithinAbs(distance_arr_val2, .001));
}

TEST_CASE("Validate TAP to Protobuf conversion with optional strong type")
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  Tap<Tachyon<testing::MsgWithOptionalAu>> tap_source;
  constexpr double distance_val = 22.678;

  // Test case 1: Optional value is set
  tap_source.set_distance(au::meters(distance_val));
  proto_testing::MsgWithOptionalAu proto_destination_set;
  testing::tap_to_protobuf(proto_destination_set, tap_source);

  REQUIRE(proto_destination_set.has_distance());
  CHECK_THAT(proto_destination_set.distance(), Catch::Matchers::WithinAbs(distance_val, .001));

  // Test case 2: Optional value is not set
  tap_source.reset_distance(); // Explicitly reset
  proto_testing::MsgWithOptionalAu proto_destination_unset;
  testing::tap_to_protobuf(proto_destination_unset, tap_source);

  CHECK_FALSE(proto_destination_unset.has_distance());
}

TEST_CASE("Validate TAP to Protobuf conversion with VarSoa")
{
  constexpr int64_t seqno1 = 111;
  constexpr int32_t other_number1 = 222;
  constexpr int64_t seqno2 = 333;
  constexpr int32_t other_number2 = 444;

  Tap<Tachyon<demo::BetterThanInheritance>> tap_source;
  auto& var_soa = tap_source.get_mutable_var_soa_composition();
  var_soa.resize(2);
  var_soa[0].set_seqno(seqno1);
  var_soa[0].set_other_number(other_number1);
  var_soa[1].set_seqno(seqno2);
  var_soa[1].set_other_number(other_number2);

  my_proto::BetterThanInheritance proto_destination;
  demo::tap_to_protobuf(proto_destination, tap_source);

  REQUIRE(proto_destination.var_soa_composition_size() == 2);
  CHECK(proto_destination.var_soa_composition(0).seqno() == seqno1);
  CHECK(proto_destination.var_soa_composition(0).other_number() == other_number1);
  CHECK(proto_destination.var_soa_composition(1).seqno() == seqno2);
  CHECK(proto_destination.var_soa_composition(1).other_number() == other_number2);
}

TEST_CASE("Validate TAP to Protobuf conversion with FixedSoa")
{
  constexpr int64_t seqno1 = 555;
  constexpr int32_t other_number1 = 666;
  constexpr int64_t seqno2 = 777;
  constexpr int32_t other_number2 = 888;

  Tap<Tachyon<demo::BetterThanInheritance>> tap_source;
  auto& fixed_soa = tap_source.get_mutable_fixed_soa_composition();
  fixed_soa[0].set_seqno(seqno1);
  fixed_soa[0].set_other_number(other_number1);
  fixed_soa[1].set_seqno(seqno2);
  fixed_soa[1].set_other_number(other_number2);

  my_proto::BetterThanInheritance proto_destination;
  demo::tap_to_protobuf(proto_destination, tap_source);

  REQUIRE(proto_destination.fixed_soa_composition_size() == 2);
  CHECK(proto_destination.fixed_soa_composition(0).seqno() == seqno1);
  CHECK(proto_destination.fixed_soa_composition(0).other_number() == other_number1);
  CHECK(proto_destination.fixed_soa_composition(1).seqno() == seqno2);
  CHECK(proto_destination.fixed_soa_composition(1).other_number() == other_number2);
}

TEST_CASE("Validate TAP to Protobuf conversion with empty VarSoa")
{
  Tap<Tachyon<demo::BetterThanInheritance>> tap_source;

  my_proto::BetterThanInheritance proto_destination;
  demo::tap_to_protobuf(proto_destination, tap_source);

  CHECK(proto_destination.var_soa_composition_size() == 0);
}

} // namespace clockwork
