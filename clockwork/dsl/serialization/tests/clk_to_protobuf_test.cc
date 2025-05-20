// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/dsl/tests/support/better_than_inheritance.pb.h"
#include "clockwork/dsl/tests/support/proto_tester_onboard.pb.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

namespace clockwork::my_proto
{

// We just want to make sure the conversion from clockwork to protobuf worked as expected
TEST_CASE("Make sure all the fields exist")
{
  BetterThanInheritance better_than_inheritance;
  better_than_inheritance.set_a_field(int64_t{56789});
  CHECK_FALSE(better_than_inheritance.has_my_optional());
  better_than_inheritance.set_my_optional(int64_t{98765});
  better_than_inheritance.set_my_string("Hello proto");
  better_than_inheritance.set_my_bytes("somebytes");
  CHECK(better_than_inheritance.has_my_bytes());
  auto* hello_msg = better_than_inheritance.add_hello();
  hello_msg->set_seqno(int64_t{123456});
  hello_msg->set_other_number(int32_t{42});

  auto* generic_member = better_than_inheritance.mutable_my_generic();
  generic_member->add_data(1.234f);
  generic_member->add_data(5.678f);
  generic_member->set_my_hello(HelloEnum::ni_hao);
}
} // namespace clockwork::my_proto
