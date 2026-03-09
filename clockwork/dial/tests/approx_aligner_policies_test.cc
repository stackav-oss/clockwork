// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/dial/approx_aligner_policies.hh"
#include "clockwork/dial/tests/support/approx_aligner_fixture.hh"

#include <boost/iterator/iterator_facade.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <limits>

namespace clockwork
{
namespace
{

using AlignerPolicies = ApproxAlignerPolicies<Input0, Input1, Input2>;

TEST_CASE_METHOD(TestApproxAlignerFixture, "validate", "[Policies::always_valid_input]")
{
  auto makers = AlignerInputMakers<Aligner, Input0, Input1, Input2>();
  auto inputs = makers.make_inputs();

  REQUIRE(AlignerPolicies::always_valid_inputs(inputs));
}

TEST_CASE_METHOD(TestApproxAlignerFixture, "validate", "[Policies::monotonically_increasing_inputs]")
{
  SECTION("no inputs")
  {
    auto makers = AlignerInputMakers<Aligner, Input0, Input1, Input2>();
    auto inputs = makers.make_inputs();

    REQUIRE(AlignerPolicies::monotonically_increasing_inputs(inputs));
  }

  SECTION("monotonic")
  {
    auto makers = AlignerInputMakers<Aligner, Input0, Input1, Input2>(
      {{.value = 10}, {.value = 11}}, {{.value = 20}, {.value = 22}}, {{.value = 30}, {.value = 33}});
    auto inputs = makers.make_inputs();
    REQUIRE(AlignerPolicies::monotonically_increasing_inputs(inputs));
  }

  SECTION("input0 not monotonic")
  {
    auto makers = AlignerInputMakers<Aligner, Input0, Input1, Input2>(
      {{.value = 20}, {.value = 11}}, {{.value = 20}, {.value = 22}}, {{.value = 30}, {.value = 33}});
    auto inputs = makers.make_inputs();
    REQUIRE_FALSE(AlignerPolicies::monotonically_increasing_inputs(inputs));
  }

  SECTION("input1 not monotonic")
  {
    auto makers = AlignerInputMakers<Aligner, Input0, Input1, Input2>(
      {{.value = 10}, {.value = 11}}, {{.value = 30}, {.value = 22}}, {{.value = 30}, {.value = 33}});
    auto inputs = makers.make_inputs();
    REQUIRE_FALSE(AlignerPolicies::monotonically_increasing_inputs(inputs));
  }

  SECTION("input2 not monotonic")
  {
    auto makers = AlignerInputMakers<Aligner, Input0, Input1, Input2>(
      {{.value = 10}, {.value = 11}}, {{.value = 20}, {.value = 22}}, {{.value = 40}, {.value = 33}});
    auto inputs = makers.make_inputs();
    REQUIRE_FALSE(AlignerPolicies::monotonically_increasing_inputs(inputs));
  }

  SECTION("input0 equal not monotonic")
  {
    auto makers = AlignerInputMakers<Aligner, Input0, Input1, Input2>(
      {{.value = 10}, {.value = 10}}, {{.value = 20}, {.value = 22}}, {{.value = 30}, {.value = 33}});
    auto inputs = makers.make_inputs();
    REQUIRE_FALSE(AlignerPolicies::monotonically_increasing_inputs(inputs));
  }
}

TEST_CASE_METHOD(TestApproxAlignerFixture, "less_than", "[Policies::less_than]")
{
  SECTION("all less than")
  {
    auto lhs = IndexArray{0, 1, 2};
    auto rhs = IndexArray{1, 2, 3};
    REQUIRE(AlignerPolicies::array_less_than(lhs, rhs));
  }

  SECTION("last less than")
  {
    auto lhs = IndexArray{0, 1, 2};
    auto rhs = IndexArray{0, 1, 3};
    REQUIRE(AlignerPolicies::array_less_than(lhs, rhs));
  }

  SECTION("first less than")
  {
    auto lhs = IndexArray{0, 2, 4};
    auto rhs = IndexArray{1, 1, 3};
    REQUIRE(AlignerPolicies::array_less_than(lhs, rhs));
  }

  SECTION("all greater than")
  {
    auto lhs = IndexArray{2, 2, 2};
    auto rhs = IndexArray{1, 1, 1};
    REQUIRE_FALSE(AlignerPolicies::array_less_than(lhs, rhs));
  }
}

TEST_CASE_METHOD(TestApproxAlignerFixture, "objective", "[Policies::exact_alignment_objective]")
{
  SECTION("0/3 aligned (i.e. no inputs)")
  {
    auto value_ptrs = ValuePtrArray{};
    REQUIRE(static_cast<ValueType>(input_count) == AlignerPolicies::exact_alignment_objective(value_ptrs));
  }

  SECTION("1/3 aligned")
  {
    {
      auto values = std::array<ValueType, input_count>{1, 2, 3};
      auto value_ptrs = ValuePtrArray{&values.at(0), &values.at(1), &values.at(2)};
      REQUIRE(2 == AlignerPolicies::exact_alignment_objective(value_ptrs));
    }

    {
      auto values = std::array<ValueType, input_count>{1, 2, 3};
      auto value_ptrs = ValuePtrArray{nullptr, &values.at(1), nullptr};
      REQUIRE(2 == AlignerPolicies::exact_alignment_objective(value_ptrs));
    }
  }

  SECTION("2/3 aligned")
  {
    {
      auto values = std::array<ValueType, input_count>{1, 1, 3};
      auto value_ptrs = ValuePtrArray{&values.at(0), &values.at(1), &values.at(2)};
      REQUIRE(1 == AlignerPolicies::exact_alignment_objective(value_ptrs));
    }

    {
      auto values = std::array<ValueType, input_count>{1, 3, 1};
      auto value_ptrs = ValuePtrArray{&values.at(0), &values.at(1), &values.at(2)};
      REQUIRE(1 == AlignerPolicies::exact_alignment_objective(value_ptrs));
    }

    {
      auto values = std::array<ValueType, input_count>{3, 1, 1};
      auto value_ptrs = ValuePtrArray{&values.at(0), &values.at(1), &values.at(2)};
      REQUIRE(1 == AlignerPolicies::exact_alignment_objective(value_ptrs));
    }

    {
      auto values = std::array<ValueType, input_count>{1, 1, 3};
      auto value_ptrs = ValuePtrArray{&values.at(0), &values.at(1), nullptr};
      REQUIRE(1 == AlignerPolicies::exact_alignment_objective(value_ptrs));
    }
  }

  SECTION("3/3 aligned")
  {
    auto values = std::array<ValueType, input_count>{1, 1, 1};
    auto value_ptrs = ValuePtrArray{&values.at(0), &values.at(1), &values.at(2)};
    REQUIRE(0 == AlignerPolicies::exact_alignment_objective(value_ptrs));
  }
}

TEST_CASE_METHOD(DoubleTestApproxAlignerFixture, "objective", "[Policies::variance_objective]")
{
  SECTION("0/3 aligned (i.e. no inputs)")
  {
    auto value_ptrs = ValuePtrArray{};
    REQUIRE(std::numeric_limits<ValueType>::max() == DoubleAlignerPolicies::variance_objective(value_ptrs));
  }

  SECTION("1/3 aligned")
  {
    {
      auto values = std::array<ValueType, input_count>{1, 2, 3};
      auto value_ptrs = ValuePtrArray{&values.at(0), &values.at(1), &values.at(2)};
      REQUIRE((2.0 / 3.0) == DoubleAlignerPolicies::variance_objective(value_ptrs));
    }

    {
      auto values = std::array<ValueType, input_count>{1, 2, 3};
      auto value_ptrs = ValuePtrArray{nullptr, &values.at(1), nullptr};
      REQUIRE(0.0 == DoubleAlignerPolicies::variance_objective(value_ptrs));
    }
  }

  SECTION("2/3 aligned")
  {
    {
      auto values = std::array<ValueType, input_count>{1, 1, 4};
      auto value_ptrs = ValuePtrArray{&values.at(0), &values.at(1), &values.at(2)};
      REQUIRE(2.0 == DoubleAlignerPolicies::variance_objective(value_ptrs));
    }

    {
      auto values = std::array<ValueType, input_count>{1, 4, 1};
      auto value_ptrs = ValuePtrArray{&values.at(0), &values.at(1), &values.at(2)};
      REQUIRE(2.0 == DoubleAlignerPolicies::variance_objective(value_ptrs));
    }

    {
      auto values = std::array<ValueType, input_count>{4, 1, 1};
      auto value_ptrs = ValuePtrArray{&values.at(0), &values.at(1), &values.at(2)};
      REQUIRE(2.0 == DoubleAlignerPolicies::variance_objective(value_ptrs));
    }

    {
      auto values = std::array<ValueType, input_count>{1, 1, 4};
      auto value_ptrs = ValuePtrArray{&values.at(0), &values.at(1), nullptr};
      REQUIRE(0.0 == DoubleAlignerPolicies::variance_objective(value_ptrs));
    }
  }

  SECTION("3/3 aligned")
  {
    auto values = std::array<ValueType, input_count>{1, 1, 1};
    auto value_ptrs = ValuePtrArray{&values.at(0), &values.at(1), &values.at(2)};
    REQUIRE(0.0 == DoubleAlignerPolicies::variance_objective(value_ptrs));
  }
}

TEST_CASE_METHOD(DoubleTestApproxAlignerFixture, "objective", "[Policies::follower_objective]")
{
  SECTION("0/3 aligned (i.e. no inputs)")
  {
    auto value_ptrs = ValuePtrArray{};
    CHECK(std::numeric_limits<ValueType>::max() == DoubleAlignerPolicies::follower_objective(value_ptrs));
  }

  SECTION("1/3 aligned")
  {
    {
      auto values = std::array<ValueType, input_count>{3, 1, 2};
      auto value_ptrs = ValuePtrArray{&values.at(0), &values.at(1), &values.at(2)};
      CHECK(DoubleAlignerPolicies::follower_objective(value_ptrs) == Catch::Approx(2.0));
    }

    {
      auto values = std::array<ValueType, input_count>{3, 1, 2};
      auto value_ptrs = ValuePtrArray{nullptr, &values.at(1), nullptr};
      CHECK(DoubleAlignerPolicies::follower_objective(value_ptrs) == std::numeric_limits<ValueType>::max());
    }
  }

  SECTION("2/3 aligned")
  {
    {
      auto values = std::array<ValueType, input_count>{2, 1, 3};
      auto value_ptrs = ValuePtrArray{&values.at(0), &values.at(1), &values.at(2)};
      CHECK(DoubleAlignerPolicies::follower_objective(value_ptrs) == Catch::Approx(1.0));
    }

    {
      auto values = std::array<ValueType, input_count>{2, 3, 1};
      auto value_ptrs = ValuePtrArray{&values.at(0), &values.at(1), &values.at(2)};
      CHECK(DoubleAlignerPolicies::follower_objective(value_ptrs) == Catch::Approx(1.0));
    }
  }

  SECTION("3/3 aligned")
  {
    {
      auto values = std::array<ValueType, input_count>{1, 1, 1};
      auto value_ptrs = ValuePtrArray{&values.at(0), &values.at(1), &values.at(2)};
      CHECK(DoubleAlignerPolicies::follower_objective(value_ptrs) == Catch::Approx(0.0));
    }

    {
      auto values = std::array<ValueType, input_count>{1, 2, 3};
      auto value_ptrs = ValuePtrArray{&values.at(0), &values.at(1), &values.at(2)};
      CHECK(DoubleAlignerPolicies::follower_objective(value_ptrs) == Catch::Approx(-1.0));
    }
  }
}

} // namespace
} // namespace clockwork
