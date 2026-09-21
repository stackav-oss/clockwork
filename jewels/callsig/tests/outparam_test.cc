// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/memory/pointers.hh"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

namespace jewels
{
namespace
{

// Test struct for more complex output parameter scenarios
struct TestData
{
  int value = 0;
  std::string name;

  bool operator==(const TestData& other) const
  {
    return value == other.value && name == other.name;
  }
};

// Test struct for FactoryResult testing (not default constructible)
struct NonDefaultConstructible
{
  int id;
  std::string data;

  // No default constructor
  NonDefaultConstructible(int id_val, std::string data_val)
    : id(id_val), data(std::move(data_val))
  {
  }

  bool operator==(const NonDefaultConstructible& other) const
  {
    return id == other.id && data == other.data;
  }
};

template <template <typename> class OutLike>
void test_outlike_construction_and_usage()
{
  SECTION("Construction from reference")
  {
    int value = 42;
    OutLike<int> out_param(value);

    REQUIRE(out_param.get() == &value);
    REQUIRE(*out_param == 42);
  }

  SECTION("Modification through Out")
  {
    int value = 10;
    OutLike<int> out_param(value);

    *out_param = 20;
    REQUIRE(value == 20);
    REQUIRE(*out_param == 20);
  }

  SECTION("Complex type access")
  {
    TestData data{.value = 42, .name = "test"};
    OutLike<TestData> out_param(data);

    REQUIRE(out_param->value == 42);
    REQUIRE(out_param->name == "test");

    out_param->value = 100;
    out_param->name = "modified";

    REQUIRE(data.value == 100);
    REQUIRE(data.name == "modified");
  }
}

TEST_CASE("Out construction and basic usage")
{
  test_outlike_construction_and_usage<Out>();
}

TEST_CASE("InOut construction and basic usage")
{
  test_outlike_construction_and_usage<InOut>();

  SECTION("Reading input from InOut")
  {
    int value = 10;
    InOut<int> in_out_param(value);

    REQUIRE(*in_out_param == 10);
  }
}

template <template <typename> class OutLike>
void test_deduction_guide()
{
  SECTION("Automatic type deduction")
  {
    int value = 42;
    auto out_param = OutLike{value};

    static_assert(std::is_same_v<decltype(out_param), OutLike<int>>);
    REQUIRE(*out_param == 42);
  }

  SECTION("Complex type deduction")
  {
    TestData data{.value = 42, .name = "test"};
    auto out_param = OutLike{data};

    static_assert(std::is_same_v<decltype(out_param), OutLike<TestData>>);
    REQUIRE(out_param->value == 42);
    REQUIRE(out_param->name == "test");
  }
}

TEST_CASE("Out deduction guide")
{
  test_deduction_guide<Out>();
}

TEST_CASE("InOut deduction guide")
{
  test_deduction_guide<InOut>();
}

template <template <typename> class OptionalOutLike>
// This works around the limitation that TEMPLATE_TEST_CASE can't take template-template parameters.
// NOLINTNEXTLINE(readability-function-size)
void test_optional_outlike_construction_and_usage()
{
  SECTION("Construction with reference (output desired)")
  {
    int value = 42;
    OptionalOutLike<int> opt_out(value);

    REQUIRE(opt_out.has_value());
    REQUIRE(static_cast<bool>(opt_out));
    REQUIRE(opt_out.get() == &value);
    REQUIRE(*opt_out == 42);
  }

  SECTION("Construction with nullopt (output not desired)")
  {
    OptionalOutLike<int> opt_out(std::nullopt);

    REQUIRE_FALSE(opt_out.has_value());
    REQUIRE_FALSE(static_cast<bool>(opt_out));
    REQUIRE(opt_out.get() == nullptr);
  }

  SECTION("Modification through OptionalOut when output is desired")
  {
    int value = 10;
    OptionalOutLike<int> opt_out(value);

    *opt_out = 20;
    REQUIRE(value == 20);
    REQUIRE(*opt_out == 20);
  }

  SECTION("Construction from optional")
  {
    std::optional<int> maybe_value;
    SECTION("with value (output desired)")
    {
      const int value{18};
      maybe_value.emplace(value);
      OptionalOutLike<int> opt_out(maybe_value);

      REQUIRE(opt_out.has_value());
      REQUIRE(static_cast<bool>(opt_out));
      REQUIRE(opt_out.get() == &(*maybe_value));
      // NOLINTNEXTLINE(clang-analyzer-core.UndefinedBinaryOperatorResult) TODO(OI-3674)
      REQUIRE(*opt_out == 18);
    }

    SECTION("without value (output not desired)")
    {
      OptionalOutLike<int> opt_out(maybe_value);

      REQUIRE_FALSE(opt_out.has_value());
      REQUIRE_FALSE(static_cast<bool>(opt_out));
      REQUIRE(opt_out.get() == nullptr);
    }
  }

  SECTION("Construction from another OptionalOut (output may or may not be desired)")
  {
    SECTION("with value (output desired)")
    {
      // make an OptionalOut with a value
      int value{18};
      OptionalOutLike<int> original_opt_out(value);

      // sanity check
      REQUIRE(original_opt_out.has_value());
      REQUIRE(original_opt_out.get() == &value);
      REQUIRE(*original_opt_out == 18);

      // construct from the other one
      OptionalOutLike<int> opt_out(original_opt_out);
      REQUIRE(opt_out.has_value());
      REQUIRE(opt_out.get() == &value);
      REQUIRE(*opt_out == 18);
    }

    SECTION("without value (output not desired)")
    {
      // make an empty OptionalOut
      OptionalOutLike<int> original_opt_out(std::nullopt);

      // sanity check
      REQUIRE_FALSE(original_opt_out.has_value());
      REQUIRE_FALSE(static_cast<bool>(original_opt_out));
      REQUIRE(original_opt_out.get() == nullptr);

      // construct from the other one
      OptionalOutLike<int> opt_out(original_opt_out);
      REQUIRE_FALSE(opt_out.has_value());
      REQUIRE_FALSE(static_cast<bool>(opt_out));
      REQUIRE(opt_out.get() == nullptr);
    }
  }
}

TEST_CASE("OptionalOut construction and basic usage")
{
  test_optional_outlike_construction_and_usage<OptionalOut>();
}

TEST_CASE("OptionalInOut construction and basic usage")
{
  test_optional_outlike_construction_and_usage<OptionalInOut>();

  SECTION("Reading input from OptionalInOut")
  {
    int value = 10;
    OptionalInOut<int> opt_in_out(value);

    REQUIRE(*opt_in_out == 10);
  }
}

template <template <typename> class OptionalOutLike>
void test_optional_outlike_deduction_guide()
{
  SECTION("Automatic type deduction with reference")
  {
    int value = 42;
    auto opt_out = OptionalOutLike{value};

    static_assert(std::is_same_v<decltype(opt_out), OptionalOutLike<int>>);
    REQUIRE(opt_out.has_value());
    REQUIRE(*opt_out == 42);
  }

  SECTION("Complex type deduction")
  {
    TestData data{.value = 42, .name = "test"};
    auto opt_out = OptionalOutLike{data};

    static_assert(std::is_same_v<decltype(opt_out), OptionalOutLike<TestData>>);
    REQUIRE(opt_out.has_value());
    REQUIRE(opt_out->value == 42);
    REQUIRE(opt_out->name == "test");
  }
}

TEST_CASE("OptionalOut deduction guide")
{
  test_optional_outlike_deduction_guide<OptionalOut>();
}

TEST_CASE("OptionalInOut deduction guide")
{
  test_optional_outlike_deduction_guide<OptionalInOut>();
}

TEST_CASE("FactoryResult construction and basic usage")
{
  SECTION("Default construction (empty)")
  {
    FactoryResult<int> factory_out;

    REQUIRE_FALSE(factory_out.has_value());
    REQUIRE_FALSE(static_cast<bool>(factory_out));
    REQUIRE(factory_out.get() == nullptr);
  }

  SECTION("Emplace with simple type")
  {
    FactoryResult<int> factory_out;

    int& result = factory_out.emplace(42);

    REQUIRE(factory_out.has_value());
    REQUIRE(static_cast<bool>(factory_out));
    REQUIRE(factory_out.get() != nullptr);
    REQUIRE(*factory_out == 42);
    REQUIRE(&result == factory_out.get());
  }

  SECTION("Emplace with complex type")
  {
    FactoryResult<TestData> factory_out;

    TestData& result = factory_out.emplace();
    result.value = 100;
    result.name = "factory";

    REQUIRE(factory_out.has_value());
    REQUIRE(factory_out->value == 100);
    REQUIRE(factory_out->name == "factory");
  }

  SECTION("Emplace with non-default-constructible type")
  {
    FactoryResult<NonDefaultConstructible> factory_out;

    auto& result = factory_out.emplace(123, "test_data");

    REQUIRE(factory_out.has_value());
    REQUIRE(factory_out->id == 123);
    REQUIRE(factory_out->data == "test_data");
    REQUIRE(&result == factory_out.get());
  }

  SECTION("Multiple emplace calls (replacement)")
  {
    FactoryResult<int> factory_out;

    factory_out.emplace(10);
    REQUIRE(*factory_out == 10);

    factory_out.emplace(20);
    REQUIRE(*factory_out == 20);
  }
}

TEST_CASE("FactoryResult reset functionality")
{
  SECTION("Reset after emplace")
  {
    FactoryResult<int> factory_out;

    factory_out.emplace(42);
    REQUIRE(factory_out.has_value());

    factory_out.reset();
    REQUIRE_FALSE(factory_out.has_value());
    REQUIRE(factory_out.get() == nullptr);
  }

  SECTION("Reset when already empty")
  {
    FactoryResult<int> factory_out;

    REQUIRE_FALSE(factory_out.has_value());
    factory_out.reset(); // Should be safe
    REQUIRE_FALSE(factory_out.has_value());
  }

  SECTION("Emplace after reset")
  {
    FactoryResult<int> factory_out;

    factory_out.emplace(10);
    factory_out.reset();
    factory_out.emplace(20);

    REQUIRE(factory_out.has_value());
    REQUIRE(*factory_out == 20);
  }
}

TEST_CASE("FactoryResult assignment operator")
{
  SECTION("Assignment to empty FactoryResult")
  {
    FactoryResult<int> factory_out;

    factory_out = 42;

    REQUIRE(factory_out.has_value());
    REQUIRE(*factory_out == 42);
  }

  SECTION("Assignment to FactoryResult with existing value")
  {
    FactoryResult<int> factory_out;
    factory_out.emplace(10);

    factory_out = 20;

    REQUIRE(factory_out.has_value());
    REQUIRE(*factory_out == 20);
  }

  SECTION("Assignment with complex type")
  {
    FactoryResult<TestData> factory_out;

    const TestData data{.value = 100, .name = "assigned"};
    factory_out = data;

    REQUIRE(factory_out.has_value());
    REQUIRE(factory_out->value == 100);
    REQUIRE(factory_out->name == "assigned");
  }

  SECTION("Designated initializer assignment with complex type")
  {
    FactoryResult<TestData> factory_out;

    factory_out = {.value = 100, .name = "assigned"};

    REQUIRE(factory_out.has_value());
    REQUIRE(factory_out->value == 100);
    REQUIRE(factory_out->name == "assigned");
  }

  SECTION("Assignment with move semantics")
  {
    FactoryResult<std::string> factory_out;

    std::string data = "move_test";
    factory_out = std::move(data);

    REQUIRE(factory_out.has_value());
    REQUIRE(*factory_out == "move_test");
  }
}

TEST_CASE("FactoryResult const correctness")
{
  SECTION("Const access to emplaced value")
  {
    FactoryResult<int> factory_out;
    factory_out.emplace(42);

    const auto& const_factory_out = factory_out;

    REQUIRE(const_factory_out.has_value());
    REQUIRE(*const_factory_out == 42);
    REQUIRE(const_factory_out.get() != nullptr);
  }

  SECTION("Const access to complex type")
  {
    FactoryResult<NonDefaultConstructible> factory_out;
    factory_out.emplace(123, "test");

    const auto& const_factory_out = factory_out;

    REQUIRE(const_factory_out->id == 123);
    REQUIRE(const_factory_out->data == "test");
  }
}

TEST_CASE("Type traits")
{
  SECTION("is_out_param_type_v trait")
  {
    static_assert(is_out_param_type_v<Out<int>>);
    static_assert(is_out_param_type_v<InOut<int>>);
    static_assert(is_out_param_type_v<OptionalOut<int>>);
    static_assert(is_out_param_type_v<OptionalInOut<int>>);
    static_assert(is_out_param_type_v<MaybeOut<int>>);
    static_assert(is_out_param_type_v<FactoryOut<int>>);
    static_assert(is_out_param_type_v<Out<TestData>>);
    static_assert(is_out_param_type_v<InOut<TestData>>);
    static_assert(is_out_param_type_v<OptionalOut<TestData>>);
    static_assert(is_out_param_type_v<OptionalInOut<TestData>>);
    static_assert(is_out_param_type_v<FactoryOut<TestData>>);

    static_assert(!is_out_param_type_v<int>);
    static_assert(!is_out_param_type_v<int*>);
    static_assert(!is_out_param_type_v<std::optional<int>>);
  }

  SECTION("OutParamType concept")
  {
    static_assert(OutParamType<Out<int>>);
    static_assert(OutParamType<InOut<int>>);
    static_assert(OutParamType<OptionalOut<int>>);
    static_assert(OutParamType<OptionalInOut<int>>);
    static_assert(OutParamType<MaybeOut<int>>);
    static_assert(OutParamType<FactoryOut<int>>);
    static_assert(OutParamType<Out<TestData>>);
    static_assert(OutParamType<InOut<TestData>>);
    static_assert(OutParamType<OptionalOut<TestData>>);
    static_assert(OutParamType<OptionalInOut<TestData>>);
    static_assert(OutParamType<FactoryOut<TestData>>);

    static_assert(!OutParamType<int>);
    static_assert(!OutParamType<int*>);
    static_assert(!OutParamType<std::optional<int>>);
  }
}

// Example usage functions to demonstrate the API

BinaryOutcome example_function_with_out(Out<int> result_out)
{
  *result_out = 42;
  return success;
}

BinaryOutcome example_function_with_in_out(InOut<int> in_out)
{
  *in_out = *in_out + 42;
  return success;
}

BinaryOutcome example_function_with_optional_out(int input, OptionalOut<std::string> message_out)
{
  if (!message_out.has_value())
  {
    // If no output is desired, just return success
    return success;
  }

  if (input > 0)
  {
    *message_out = "Positive input";
    return success;
  }

  *message_out = "Non-positive input";
  return failure;
}

BinaryOutcome example_function_with_optional_in_out(OptionalInOut<int> state)
{
  if (!state.has_value())
  {
    // If no output is desired, just return success
    return success;
  }

  if (*state > 0)
  {
    *state *= 2;
    return success;
  }

  *state = 0;
  return failure;
}

BinaryOutcome example_function_with_factory_out(int input, FactoryOut<NonDefaultConstructible> result_out)
{
  if (input <= 0)
  {
    return failure;
  }

  result_out->emplace(input, "Generated by factory");
  return success;
}

TEST_CASE("Example usage scenarios")
{
  SECTION("Standard output parameter usage")
  {
    int result = 0;
    auto outcome = example_function_with_out(Out{result});

    REQUIRE(ok(outcome));
    REQUIRE(result == 42);
  }

  SECTION("Standard input/output parameter usage")
  {
    int state = 1000;
    auto outcome = example_function_with_in_out(InOut{state});

    REQUIRE(ok(outcome));
    REQUIRE(state == 1042); // 1000 + 42
  }

  SECTION("Optional output parameter - output desired")
  {
    std::string message;
    auto outcome = example_function_with_optional_out(5, OptionalOut{message});

    REQUIRE(ok(outcome));
    REQUIRE(message == "Positive input");
  }

  SECTION("Optional output parameter - output not desired")
  {
    auto outcome = example_function_with_optional_out(5, std::nullopt);

    REQUIRE(ok(outcome));
    // No output to check since it wasn't requested
  }

  SECTION("Optional input/output parameter - output desired")
  {
    int state = 5;
    auto outcome = example_function_with_optional_in_out(OptionalInOut{state});

    REQUIRE(ok(outcome));
    REQUIRE(state == 10); // 5 * 2
  }

  SECTION("Optional input/output parameter - output not desired")
  {
    auto outcome = example_function_with_optional_in_out(std::nullopt);

    REQUIRE(ok(outcome));
    // No output to check since it wasn't requested
  }

  SECTION("Optional input/output parameter with failure case")
  {
    int state = -1;
    auto outcome = example_function_with_optional_in_out(OptionalInOut{state});

    REQUIRE(fails(outcome));
    REQUIRE(state == 0);
  }

  SECTION("Function call with failure case")
  {
    std::string message;
    auto outcome = example_function_with_optional_out(-1, OptionalOut{message});

    REQUIRE(fails(outcome));
    REQUIRE(message == "Non-positive input");
  }

  SECTION("FactoryResult parameter usage")
  {
    FactoryResult<NonDefaultConstructible> result;
    auto outcome = example_function_with_factory_out(42, Out{result});

    REQUIRE(ok(outcome));
    REQUIRE(result.has_value());
    REQUIRE(result->id == 42);
    REQUIRE(result->data == "Generated by factory");
  }

  SECTION("FactoryResult parameter with failure")
  {
    FactoryResult<NonDefaultConstructible> result;
    auto outcome = example_function_with_factory_out(-1, Out{result});

    REQUIRE(fails(outcome));
    REQUIRE_FALSE(result.has_value());
  }
}

TEST_CASE("Const correctness")
{
  SECTION("Out const access")
  {
    int value = 42;
    const Out<int> out_param(value);

    // Should be able to read through const Out
    REQUIRE(*out_param == 42);
    REQUIRE(out_param.get() == &value);

    // But modification should not compile (this is checked by the compiler)
    // *out_param = 100; // This should not compile
  }

  SECTION("InOut const access")
  {
    int value = 42;
    const InOut<int> in_out_param(value);

    // Should be able to read through const InOut
    REQUIRE(*in_out_param == 42);
    REQUIRE(in_out_param.get() == &value);

    // But modification should not compile (this is checked by the compiler)
    // *in_out_param = 100; // This should not compile
  }

  SECTION("OptionalOut const access")
  {
    int value = 42;
    const OptionalOut<int> opt_out(value);

    // Should be able to read through const OptionalOut
    REQUIRE(opt_out.has_value());
    REQUIRE(*opt_out == 42);
    REQUIRE(opt_out.get() == &value);

    // But modification should not compile (this is checked by the compiler)
    // *opt_out = 100; // This should not compile
  }

  SECTION("OptionalInOut const access")
  {
    int value = 42;
    const OptionalInOut<int> opt_in_out(value);

    // Should be able to read through const OptionalInOut
    REQUIRE(opt_in_out.has_value());
    REQUIRE(*opt_in_out == 42);
    REQUIRE(opt_in_out.get() == &value);

    // But modification should not compile (this is checked by the compiler)
    // *opt_in_out = 100; // This should not compile
  }
}

// Function definition
void try_compute_derived(MaybeOut<int32_t>& maybe_out, const bool set_maybe_out)
{
  if (set_maybe_out)
  {
    *maybe_out = 42;
    // auto-marked as valid by dereferencing
  }
  else
  {
    maybe_out.mark_invalid();
  }
}

TEST_CASE("MaybeOut")
{
  int32_t out_data{};
  jewels::MaybeOut maybe_out{out_data};

  // Call site
  try_compute_derived(maybe_out, true);
  REQUIRE(maybe_out.is_valid());
  REQUIRE(out_data == 42);
  REQUIRE(*maybe_out == 42);
  REQUIRE(maybe_out.get() != nullptr);
  REQUIRE(&(*maybe_out) == &(out_data));

  // call without setting maybe out
  try_compute_derived(maybe_out, false);
  REQUIRE_FALSE(maybe_out.is_valid());
  REQUIRE(maybe_out.get() != nullptr); // Callee-determined - even if it's not set, caller must provide valid memory.
}

} // anonymous namespace
} // namespace jewels
