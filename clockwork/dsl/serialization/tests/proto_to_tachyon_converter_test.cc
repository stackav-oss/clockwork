// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/dsl/tests/support/better_than_inheritance.pb.h"
#include "clockwork/dsl/tests/support/better_than_inheritance_cpp.hh"
#include "clockwork/dsl/tests/support/convert_hello_msg.hh"
#include "clockwork/dsl/tests/support/dependency_tester_cpp.hh"
#include "clockwork/dsl/tests/support/dependency_tester_proto.pb.h"
#include "clockwork/dsl/tests/support/msg_with_au.hh"
#include "clockwork/dsl/tests/support/msg_with_au_proto.pb.h"
#include "clockwork/dsl/tests/support/proto_tester_onboard.pb.h"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <au/quantity.hh>
#include <au/units/meters.hh>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <google/protobuf/duration.pb.h>
#include <google/protobuf/timestamp.pb.h>

#include <chrono>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <tuple>

namespace clockwork
{
TEST_CASE("Validate Conversion")
{
  using namespace std::chrono_literals;
  constexpr int64_t a_field = 43;
  constexpr int64_t my_optional = 23442334;
  auto my_uuid = ::jewels::Uuid<demo::BetterThanInheritance>::random_uuid();
  std::string my_string = "This is a string";
  std::string my_bytes = "ssfesvb";
  constexpr uint32_t funny_number = 12;
  constexpr jewels::time::SyncTime my_sync_time{3s};
  constexpr auto a_duration{1000ns};
  my_proto::proto_tester proto_tester_source1;

  constexpr int64_t seqno1 = 124253;
  constexpr int32_t other_number1 = 45;

  proto_tester_source1.set_seqno(seqno1);
  proto_tester_source1.set_other_number(other_number1);

  my_proto::proto_tester proto_tester_source2;

  constexpr int64_t seqno2 = 245;
  constexpr int32_t other_number2 = 542;

  constexpr int64_t primitive_one = 13134;
  constexpr int64_t primitive_two = 413278;

  proto_tester_source2.set_seqno(seqno2);
  proto_tester_source2.set_other_number(other_number2);

  my_proto::BetterThanInheritance better_than_inheritance_source;
  clockwork::Tap<clockwork::Tachyon<demo::BetterThanInheritance>> better_than_inheritance_destination;

  better_than_inheritance_source.set_a_field(a_field);
  better_than_inheritance_source.set_my_optional(my_optional);
  better_than_inheritance_source.set_my_uuid(my_uuid.to_string());
  better_than_inheritance_source.set_my_string(my_string);
  better_than_inheritance_source.set_my_bytes(my_bytes);
  better_than_inheritance_source.set_funny_number(funny_number);
  better_than_inheritance_source.mutable_sync_time()->set_seconds(
    std::chrono::duration_cast<std::chrono::seconds>(my_sync_time.time_since_epoch()).count());

  better_than_inheritance_source.mutable_opt_duration()->set_nanos(a_duration.count());
  better_than_inheritance_source.mutable_opt_uuid()->assign(my_uuid.to_string());

  auto* opt_composition = better_than_inheritance_source.mutable_opt_composition();
  *opt_composition = proto_tester_source2;

  auto* first_proto_tester = better_than_inheritance_source.add_composition_var();
  *first_proto_tester = proto_tester_source1;

  auto* second_proto_tester = better_than_inheritance_source.add_composition_var();
  *second_proto_tester = proto_tester_source2;

  auto enum_one = my_proto::HelloEnum::HELLO_ENUM_HI;
  auto enum_two = my_proto::HelloEnum::HELLO_ENUM_NI_HAO;

  better_than_inheritance_source.add_enum_arr(enum_one);
  better_than_inheritance_source.add_enum_arr(enum_two);

  better_than_inheritance_source.add_fixed_enum_arr(enum_one);
  better_than_inheritance_source.add_fixed_enum_arr(enum_two);

  better_than_inheritance_source.set_fixed_bytes(my_bytes);

  better_than_inheritance_source.add_fixed_primitive(primitive_one);
  better_than_inheritance_source.add_fixed_primitive(primitive_two);

  auto* first_proto_tester_fixed = better_than_inheritance_source.add_fixed_composition();
  *first_proto_tester_fixed = proto_tester_source1;

  auto* second_proto_tester_fixed = better_than_inheritance_source.add_fixed_composition();
  *second_proto_tester_fixed = proto_tester_source2;

  better_than_inheritance_source.add_var_array_of_small_int(10);
  better_than_inheritance_source.add_var_array_of_small_int(20);
  better_than_inheritance_source.add_var_array_of_small_int(-30);

  better_than_inheritance_source.add_fixed_array_of_small_int(5);
  better_than_inheritance_source.add_fixed_array_of_small_int(-10);
  better_than_inheritance_source.add_fixed_array_of_small_int(127);

  SECTION("Fail validation - incomplete")
  {
    auto status = demo::protobuf_to_tap(better_than_inheritance_destination, better_than_inheritance_source);
    REQUIRE(!status);
  }

  std::ignore = better_than_inheritance_source.mutable_my_duration();

  auto* my_generic = better_than_inheritance_source.mutable_my_generic();
  my_generic->set_my_hello(my_proto::HelloEnum::HELLO_ENUM_HOLA);

  auto* another_generic = better_than_inheritance_source.mutable_another_generic();
  another_generic->set_my_hello(my_proto::HELLO_ENUM_NI_HAO);
  auto* opt_dependency = better_than_inheritance_source.mutable_opt_dependency();
  opt_dependency->set_an_optional(funny_number);
  SECTION("Pass validation")
  {
    auto status = demo::protobuf_to_tap(better_than_inheritance_destination, better_than_inheritance_source);
    REQUIRE(status);
    CHECK(better_than_inheritance_destination.get_a_field() == a_field);
    CHECK(better_than_inheritance_destination.has_my_optional());
    CHECK(better_than_inheritance_destination.value_my_optional() == my_optional);
    CHECK(better_than_inheritance_destination.get_my_uuid() == my_uuid);
    CHECK(better_than_inheritance_destination.get_composition_var().size() == 2);
    CHECK(better_than_inheritance_destination.get_composition_var()[1].get_seqno() == seqno2);
    CHECK(better_than_inheritance_destination.get_funny_number() == funny_number);

    CHECK(better_than_inheritance_destination.get_enum_arr().size() == 2);
    CHECK(better_than_inheritance_destination.get_enum_arr()[0] == demo::HelloEnum::hi);
    CHECK(better_than_inheritance_destination.get_enum_arr()[1] == demo::HelloEnum::ni_hao);

    CHECK(better_than_inheritance_destination.get_fixed_enum_arr().size() == 2);
    CHECK(better_than_inheritance_destination.get_fixed_enum_arr()[0] == demo::HelloEnum::hi);
    CHECK(better_than_inheritance_destination.get_fixed_enum_arr()[1] == demo::HelloEnum::ni_hao);

    CHECK(better_than_inheritance_destination.get_fixed_composition().size() == 2);
    CHECK(better_than_inheritance_destination.get_fixed_composition()[1].get_seqno() == seqno2);

    CHECK(better_than_inheritance_destination.get_fixed_primitive().size() == 2);
    CHECK(better_than_inheritance_destination.get_fixed_primitive()[1] == primitive_two);

    CHECK(better_than_inheritance_destination.value_opt_composition().get_seqno() == seqno2);
    CHECK(better_than_inheritance_destination.value_sync_time() == my_sync_time);
    CHECK(better_than_inheritance_destination.value_opt_duration() == a_duration);
    CHECK(better_than_inheritance_destination.value_opt_uuid() == my_uuid);
    CHECK(better_than_inheritance_destination.get_my_generic().has_my_hello());
    CHECK(better_than_inheritance_destination.get_my_generic().value_my_hello() == demo::HelloEnum::hola);
    CHECK(better_than_inheritance_destination.value_opt_dependency().value_an_optional() == funny_number);
    CHECK(better_than_inheritance_destination.get_var_array_of_small_int().size() == 3);
    CHECK(better_than_inheritance_destination.get_var_array_of_small_int()[0] == 10);
    CHECK(better_than_inheritance_destination.get_var_array_of_small_int()[1] == 20);
    CHECK(better_than_inheritance_destination.get_var_array_of_small_int()[2] == -30);

    CHECK(better_than_inheritance_destination.get_fixed_array_of_small_int().size() == 3);
    CHECK(better_than_inheritance_destination.get_fixed_array_of_small_int()[0] == 5);
    CHECK(better_than_inheritance_destination.get_fixed_array_of_small_int()[1] == -10);
    CHECK(better_than_inheritance_destination.get_fixed_array_of_small_int()[2] == 127);

    CHECK_FALSE(better_than_inheritance_destination.has_optional_string());
  }

  SECTION("bad uuid")
  {
    better_than_inheritance_source.set_my_uuid("junk");
    auto error_status = demo::protobuf_to_tap(better_than_inheritance_destination, better_than_inheritance_source);
    CHECK_FALSE(error_status);
  }

  SECTION("int range")
  {
    constexpr int32_t big = 10'000;
    better_than_inheritance_source.set_funny_number(-big);
    CHECK(demo::protobuf_to_tap(better_than_inheritance_destination, better_than_inheritance_source));
    better_than_inheritance_source.set_funny_number(big);
    CHECK(demo::protobuf_to_tap(better_than_inheritance_destination, better_than_inheritance_source));

    constexpr int32_t too_big = 100'000;
    better_than_inheritance_source.set_funny_number(-too_big);
    CHECK_FALSE(demo::protobuf_to_tap(better_than_inheritance_destination, better_than_inheritance_source));
    better_than_inheritance_source.set_funny_number(too_big);
    CHECK_FALSE(demo::protobuf_to_tap(better_than_inheritance_destination, better_than_inheritance_source));
  }

  SECTION("optional string present")
  {
    std::string my_optional_string = "An optional string";
    better_than_inheritance_source.set_optional_string(my_optional_string);
    auto status = demo::protobuf_to_tap(better_than_inheritance_destination, better_than_inheritance_source);
    REQUIRE(status);
    CHECK(better_than_inheritance_destination.has_optional_string());
    CHECK(better_than_inheritance_destination.value_optional_string() == my_optional_string);
  }
}

TEST_CASE("Validate conversion with strong type")
{
  proto_testing::MsgWithAu source;
  constexpr double distance = 22.678;
  source.set_distance(distance);

  clockwork::Tap<clockwork::Tachyon<testing::MsgWithAu>> destination;
  CHECK(testing::protobuf_to_tap(destination, source));
  CHECK_THAT(destination.get_distance().in(au::meters), Catch::Matchers::WithinAbs(distance, .001));
}

TEST_CASE("Validate conversion with optional strong type")
{
  proto_testing::MsgWithOptionalAu source;
  constexpr double distance = 22.678;
  source.set_distance(distance);

  clockwork::Tap<clockwork::Tachyon<testing::MsgWithOptionalAu>> destination;
  CHECK(testing::protobuf_to_tap(destination, source));
  CHECK_THAT(destination.value_distance().in(au::meters), Catch::Matchers::WithinAbs(distance, .001));
}

TEST_CASE("Validate conversion with VarArray of strong type")
{
  proto_testing::MsgWithMultipleAu source;
  constexpr double distance1 = 11.1;
  constexpr double distance2 = 22.2;

  source.add_distance_array(distance1);
  source.add_distance_array(distance2);

  // Ensure other fields that are required by validation are set
  source.set_distance(1.0);
  source.set_distance2(2.0);

  clockwork::Tap<clockwork::Tachyon<testing::MsgWithMultipleAu>> destination;
  auto status = testing::protobuf_to_tap(destination, source);
  REQUIRE(status);

  REQUIRE(destination.get_distance_array().size() == 2);
  CHECK_THAT(destination.get_distance_array()[0].in(au::meters), Catch::Matchers::WithinAbs(distance1, .001));
  CHECK_THAT(destination.get_distance_array()[1].in(au::meters), Catch::Matchers::WithinAbs(distance2, .001));
}

TEST_CASE("Validate conversion with VarSoa")
{
  constexpr int64_t seqno1 = 111;
  constexpr int32_t other_number1 = 222;
  constexpr int64_t seqno2 = 333;
  constexpr int32_t other_number2 = 444;

  // Build a minimal valid BetterThanInheritance proto with VarSoa data
  my_proto::BetterThanInheritance source;
  source.set_a_field(1);
  source.set_my_uuid(::jewels::Uuid<demo::BetterThanInheritance>::random_uuid().to_string());
  source.set_my_string("");
  source.set_my_bytes("");
  source.set_funny_number(1);
  std::ignore = source.mutable_sync_time();
  std::ignore = source.mutable_my_duration();
  auto* my_generic = source.mutable_my_generic();
  my_generic->set_my_hello(my_proto::HelloEnum::HELLO_ENUM_HI);
  auto* another_generic = source.mutable_another_generic();
  another_generic->set_my_hello(my_proto::HelloEnum::HELLO_ENUM_HI);

  auto* elem1 = source.add_var_soa_composition();
  elem1->set_seqno(seqno1);
  elem1->set_other_number(other_number1);
  auto* elem2 = source.add_var_soa_composition();
  elem2->set_seqno(seqno2);
  elem2->set_other_number(other_number2);

  clockwork::Tap<clockwork::Tachyon<demo::BetterThanInheritance>> destination;
  auto status = demo::protobuf_to_tap(destination, source);
  REQUIRE(status);

  const auto& var_soa = destination.get_var_soa_composition();
  REQUIRE(var_soa.size() == 2);
  CHECK(var_soa[0].get_seqno() == seqno1);
  CHECK(var_soa[0].get_other_number() == other_number1);
  CHECK(var_soa[1].get_seqno() == seqno2);
  CHECK(var_soa[1].get_other_number() == other_number2);
}

TEST_CASE("Validate conversion with FixedSoa")
{
  constexpr int64_t seqno1 = 555;
  constexpr int32_t other_number1 = 666;
  constexpr int64_t seqno2 = 777;
  constexpr int32_t other_number2 = 888;

  my_proto::BetterThanInheritance source;
  source.set_a_field(1);
  source.set_my_uuid(::jewels::Uuid<demo::BetterThanInheritance>::random_uuid().to_string());
  source.set_my_string("");
  source.set_my_bytes("");
  source.set_funny_number(1);
  std::ignore = source.mutable_sync_time();
  std::ignore = source.mutable_my_duration();
  auto* my_generic = source.mutable_my_generic();
  my_generic->set_my_hello(my_proto::HelloEnum::HELLO_ENUM_HI);
  auto* another_generic = source.mutable_another_generic();
  another_generic->set_my_hello(my_proto::HelloEnum::HELLO_ENUM_HI);

  auto* elem1 = source.add_fixed_soa_composition();
  elem1->set_seqno(seqno1);
  elem1->set_other_number(other_number1);
  auto* elem2 = source.add_fixed_soa_composition();
  elem2->set_seqno(seqno2);
  elem2->set_other_number(other_number2);

  clockwork::Tap<clockwork::Tachyon<demo::BetterThanInheritance>> destination;
  auto status = demo::protobuf_to_tap(destination, source);
  REQUIRE(status);

  const auto& fixed_soa = destination.get_fixed_soa_composition();
  REQUIRE(fixed_soa.size() == 2);
  CHECK(fixed_soa[0].get_seqno() == seqno1);
  CHECK(fixed_soa[0].get_other_number() == other_number1);
  CHECK(fixed_soa[1].get_seqno() == seqno2);
  CHECK(fixed_soa[1].get_other_number() == other_number2);
}

TEST_CASE("Validate conversion with empty VarSoa")
{
  my_proto::BetterThanInheritance source;
  source.set_a_field(1);
  source.set_my_uuid(::jewels::Uuid<demo::BetterThanInheritance>::random_uuid().to_string());
  source.set_my_string("");
  source.set_my_bytes("");
  source.set_funny_number(1);
  std::ignore = source.mutable_sync_time();
  std::ignore = source.mutable_my_duration();
  auto* my_generic = source.mutable_my_generic();
  my_generic->set_my_hello(my_proto::HelloEnum::HELLO_ENUM_HI);
  auto* another_generic = source.mutable_another_generic();
  another_generic->set_my_hello(my_proto::HelloEnum::HELLO_ENUM_HI);

  clockwork::Tap<clockwork::Tachyon<demo::BetterThanInheritance>> destination;
  auto status = demo::protobuf_to_tap(destination, source);
  REQUIRE(status);

  CHECK(destination.get_var_soa_composition().size() == 0);
}
} // namespace clockwork
