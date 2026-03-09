// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/callsig/outcome.hh"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

namespace jewels
{
namespace
{

// Test enum for Outcome<T> tests
enum class TestResult : uint8_t
{
  success,
  insufficient_data,
  invalid_input,
  timeout,
  computation_error
};

// Test enum with multiple success values
enum class AdvancedResult : uint8_t
{
  perfect,
  good,
  acceptable,
  poor,
  failed
};

TEST_CASE("BinaryOutcome construction")
{
  SECTION("Success outcome")
  {
    const auto outcome = BinaryOutcome::make_success();
    REQUIRE(outcome.ok());
    REQUIRE_FALSE(outcome.fails());
  }

  SECTION("Failure outcome")
  {
    const auto outcome = BinaryOutcome::make_failure();
    REQUIRE_FALSE(outcome.ok());
    REQUIRE(outcome.fails());
  }

  SECTION("Global success instance")
  {
    REQUIRE(success.ok());
    REQUIRE_FALSE(success.fails());
  }

  SECTION("Global failure instance")
  {
    REQUIRE_FALSE(failure.ok());
    REQUIRE(failure.fails());
  }
}

TEST_CASE("BinaryOutcome free functions")
{
  SECTION("ok() free function with success")
  {
    const auto outcome = BinaryOutcome::make_success();
    REQUIRE(ok(outcome));
  }

  SECTION("ok() free function with failure")
  {
    const auto outcome = BinaryOutcome::make_failure();
    REQUIRE_FALSE(ok(outcome));
  }

  SECTION("fails() free function with success")
  {
    const auto outcome = BinaryOutcome::make_success();
    REQUIRE_FALSE(fails(outcome));
  }

  SECTION("fails() free function with failure")
  {
    const auto outcome = BinaryOutcome::make_failure();
    REQUIRE(fails(outcome));
  }
}

TEST_CASE("Outcome<T> basic functionality")
{
  SECTION("Construction from enum value")
  {
    const Outcome<TestResult> outcome(TestResult::success);
    REQUIRE(outcome.get() == TestResult::success);
  }

  SECTION("Implicit construction from enum")
  {
    const Outcome<TestResult> outcome = TestResult::invalid_input;
    REQUIRE(outcome.get() == TestResult::invalid_input);
  }

  SECTION("Get method returns correct value")
  {
    const Outcome<TestResult> outcome(TestResult::timeout);
    REQUIRE(outcome.get() == TestResult::timeout);
  }
}

TEST_CASE("Outcome<T, success_values...> with single success value")
{
  using SuccessOutcome = Outcome<TestResult, TestResult::success>;

  SECTION("Success case")
  {
    const SuccessOutcome outcome(TestResult::success);
    REQUIRE(outcome.get() == TestResult::success);
    REQUIRE(outcome.ok());
    REQUIRE_FALSE(outcome.fails());
  }

  SECTION("Failure cases")
  {
    const SuccessOutcome outcome1(TestResult::insufficient_data);
    REQUIRE(outcome1.get() == TestResult::insufficient_data);
    REQUIRE_FALSE(outcome1.ok());
    REQUIRE(outcome1.fails());

    const SuccessOutcome outcome2(TestResult::timeout);
    REQUIRE(outcome2.get() == TestResult::timeout);
    REQUIRE_FALSE(outcome2.ok());
    REQUIRE(outcome2.fails());
  }
}

TEST_CASE("Outcome<T, success_values...> with multiple success values")
{
  using MultiSuccessOutcome =
    Outcome<AdvancedResult, AdvancedResult::perfect, AdvancedResult::good, AdvancedResult::acceptable>;

  SECTION("All success cases")
  {
    const MultiSuccessOutcome outcome1(AdvancedResult::perfect);
    REQUIRE(outcome1.ok());
    REQUIRE_FALSE(outcome1.fails());

    const MultiSuccessOutcome outcome2(AdvancedResult::good);
    REQUIRE(outcome2.ok());
    REQUIRE_FALSE(outcome2.fails());

    const MultiSuccessOutcome outcome3(AdvancedResult::acceptable);
    REQUIRE(outcome3.ok());
    REQUIRE_FALSE(outcome3.fails());
  }

  SECTION("Failure cases")
  {
    const MultiSuccessOutcome outcome1(AdvancedResult::poor);
    REQUIRE_FALSE(outcome1.ok());
    REQUIRE(outcome1.fails());

    const MultiSuccessOutcome outcome2(AdvancedResult::failed);
    REQUIRE_FALSE(outcome2.ok());
    REQUIRE(outcome2.fails());
  }
}

TEST_CASE("Outcome free functions")
{
  using SuccessOutcome = Outcome<TestResult, TestResult::success>;

  SECTION("ok() free function with success")
  {
    const SuccessOutcome outcome(TestResult::success);
    REQUIRE(ok(outcome));
  }

  SECTION("ok() free function with failure")
  {
    const SuccessOutcome outcome(TestResult::timeout);
    REQUIRE_FALSE(ok(outcome));
  }

  SECTION("fails() free function with success")
  {
    const SuccessOutcome outcome(TestResult::success);
    REQUIRE_FALSE(fails(outcome));
  }

  SECTION("fails() free function with failure")
  {
    const SuccessOutcome outcome(TestResult::timeout);
    REQUIRE(fails(outcome));
  }
}

TEST_CASE("outcome_in function")
{
  const Outcome<TestResult> success_outcome(TestResult::success);
  const Outcome<TestResult> timeout_outcome(TestResult::timeout);
  const Outcome<TestResult> error_outcome(TestResult::computation_error);

  SECTION("Single value match")
  {
    REQUIRE(outcome_in<TestResult::success>(success_outcome));
    REQUIRE_FALSE(outcome_in<TestResult::success>(timeout_outcome));
  }

  SECTION("Multiple value match")
  {
    REQUIRE(outcome_in<TestResult::timeout, TestResult::computation_error>(timeout_outcome));
    REQUIRE(outcome_in<TestResult::timeout, TestResult::computation_error>(error_outcome));
    REQUIRE_FALSE(outcome_in<TestResult::timeout, TestResult::computation_error>(success_outcome));
  }
}

TEST_CASE("Real-world usage patterns")
{
  SECTION("Function returning BinaryOutcome")
  {
    auto validate_input = [](int value) -> BinaryOutcome
    {
      if (value > 0 && value < 100)
      {
        return success;
      }
      return failure;
    };

    REQUIRE(ok(validate_input(50)));
    REQUIRE(fails(validate_input(-1)));
    REQUIRE(fails(validate_input(150)));
  }

  SECTION("Function returning Outcome<T>")
  {
    auto process_data = [](int size) -> Outcome<TestResult>
    {
      if (size == 0)
      {
        return TestResult::insufficient_data;
      }
      if (size < 0)
      {
        return TestResult::invalid_input;
      }
      if (size > 1000)
      {
        return TestResult::timeout;
      }
      return TestResult::success;
    };

    const auto result1 = process_data(50);
    REQUIRE(result1.get() == TestResult::success);

    const auto result2 = process_data(0);
    REQUIRE(result2.get() == TestResult::insufficient_data);

    const auto result3 = process_data(-5);
    REQUIRE(result3.get() == TestResult::invalid_input);

    const auto result4 = process_data(2000);
    REQUIRE(result4.get() == TestResult::timeout);
  }

  SECTION("Function returning Outcome<T, success_values...>")
  {
    using ProcessOutcome = Outcome<TestResult, TestResult::success>;

    auto robust_process = [](int value) -> ProcessOutcome
    {
      if (value >= 0 && value <= 100)
      {
        return TestResult::success;
      }
      if (value < 0)
      {
        return TestResult::invalid_input;
      }
      return TestResult::timeout;
    };

    const auto good_result = robust_process(50);
    REQUIRE(good_result.ok());
    REQUIRE(good_result.get() == TestResult::success);

    const auto bad_result = robust_process(-10);
    REQUIRE(bad_result.fails());
    REQUIRE(bad_result.get() == TestResult::invalid_input);
  }

  SECTION("Chaining outcome checks")
  {
    auto step1 = []() -> BinaryOutcome { return success; };
    auto step2 = []() -> BinaryOutcome { return success; };
    auto step3 = []() -> BinaryOutcome { return failure; };

    auto multi_step_process = [&step1, &step2, &step3]() -> BinaryOutcome
    {
      if (fails(step1()))
      {
        return failure;
      }
      if (fails(step2()))
      {
        return failure;
      }
      if (fails(step3()))
      {
        return failure;
      }
      return success;
    };

    REQUIRE(fails(multi_step_process()));
  }
}

TEST_CASE("Constexpr functionality")
{
  SECTION("BinaryOutcome constexpr operations")
  {
    constexpr auto success_outcome = BinaryOutcome::make_success();
    constexpr auto failure_outcome = BinaryOutcome::make_failure();

    static_assert(success_outcome.ok());
    static_assert(!success_outcome.fails());
    static_assert(!failure_outcome.ok());
    static_assert(failure_outcome.fails());
  }

  SECTION("Outcome<T> constexpr operations")
  {
    constexpr Outcome<TestResult> outcome(TestResult::success);
    static_assert(outcome.get() == TestResult::success);
  }

  SECTION("Outcome<T, success_values...> constexpr operations")
  {
    constexpr Outcome<TestResult, TestResult::success> success_outcome(TestResult::success);
    constexpr Outcome<TestResult, TestResult::success> failure_outcome(TestResult::timeout);

    static_assert(success_outcome.ok());
    static_assert(!success_outcome.fails());
    static_assert(!failure_outcome.ok());
    static_assert(failure_outcome.fails());
  }

  SECTION("Global instances are constexpr")
  {
    static_assert(success.ok());
    static_assert(!success.fails());
    static_assert(!failure.ok());
    static_assert(failure.fails());
  }
}

} // anonymous namespace
} // namespace jewels
