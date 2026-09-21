// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/dial/alignment_type_clk_cc.hh"
#include "clockwork/dial/approx_aligner.hh"
#include "clockwork/dial/approx_aligner_config_clk_cc.hh"
#include "clockwork/dial/msg_input.hh"
#include "clockwork/dial/tests/support/approx_aligner_fixture.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <memory_resource>
#include <optional>
#include <ranges>
#include <sys/types.h>
#include <tuple>
#include <vector>

namespace clockwork
{
namespace
{

TEST_CASE("check test variables", "[AlignerPolicy]")
{
  using Aligner = ApproxAligner<TestAlignerPolicy, Input0, Input1, Input2>;
  REQUIRE_NOTHROW(Aligner{});
  using Policy = typename Aligner::Policy;
  using ValuePtrArray = typename Aligner::ValuePtrArray;

  auto makers = AlignerInputMakers<Aligner, Input0, Input1, Input2>();
  auto inputs = makers.make_inputs();
  auto values = ValuePtrArray{};

  Aligner::Policy::test_validate_inputs_return_ = true;
  REQUIRE(Aligner::Policy::test_validate_inputs_return_ == Policy::validate_inputs(inputs));

  Aligner::Policy::test_objective_score_return_ = 4;
  REQUIRE(Aligner::Policy::test_objective_score_return_ == Policy::objective(values));
}

TEST_CASE_METHOD(TestApproxAlignerFixture, "noop execution", "[ApproxAligner]")
{
  auto now = state.get_last_commit_timestamp();

  auto makers = AlignerInputMakers<Aligner, Input0, Input1, Input2>();
  auto inputs = makers.make_inputs();
  auto new_begins = std::apply([](auto&... input) { return InputItTuple(input.get_view().begin()...); }, inputs);

  const auto max_missing_inputs_on_timeout{GENERATE(0UL, 1UL)};
  config.set_max_missing_inputs_on_timeout(max_missing_inputs_on_timeout);

  auto result = Aligner::find_alignment(resource, config, state, inputs, now);
  REQUIRE(ApproxAlignerStateType::insufficient_data == result.state);

  REQUIRE_NOTHROW(Aligner::commit(state, inputs, result));
  REQUIRE_NOTHROW(Aligner::reset(state, inputs, now));
  REQUIRE_NOTHROW(Aligner::clear(state, inputs, now, new_begins));
}

TEST_CASE_METHOD(TestApproxAlignerFixture, "reset", "[ApproxAligner]")
{
  auto makers = AlignerInputMakers<Aligner, Input0, Input1, Input2>({{.value = 10}}, {{.value = 20}}, {{.value = 30}});
  auto inputs = makers.make_inputs();

  state.set_last_commit_timestamp(SyncTime{std::chrono::seconds{4}});

  auto now = SyncTime{std::chrono::seconds{5}};
  Aligner::reset(state, inputs, now);

  auto expected = State{};
  expected.set_last_commit_timestamp(now);
  REQUIRE(expected == state);
}

TEST_CASE_METHOD(TestApproxAlignerFixture, "clear", "[ApproxAligner]")
{
  auto makers = AlignerInputMakers<Aligner, Input0, Input1, Input2>({{.value = 10}}, {{.value = 20}}, {{.value = 30}});
  auto inputs = makers.make_inputs();
  auto new_begins = std::apply([](auto&... input) { return InputItTuple(input.get_view().begin()...); }, inputs);

  state.set_last_commit_timestamp(SyncTime{std::chrono::seconds{4}});

  auto now = SyncTime{std::chrono::seconds{5}};
  Aligner::clear(state, inputs, now, new_begins);

  auto expected = State{};
  expected.set_last_commit_timestamp(now);
  REQUIRE(expected == state);
  auto cursors = std::apply([](auto&... input) { return std::make_tuple(input.get_cursor()...); }, inputs);
  REQUIRE(new_begins == cursors);
}

TEST_CASE_METHOD(TestApproxAlignerFixture, "commit", "[ApproxAligner]")
{
  auto makers = AlignerInputMakers<Aligner, Input0, Input1, Input2>({{.value = 10}}, {{.value = 20}}, {{.value = 30}});
  auto inputs = makers.make_inputs();

  state.set_last_commit_timestamp(SyncTime{std::chrono::seconds{4}});

  auto now = SyncTime{std::chrono::seconds{5}};
  auto result = Result{
    .time_of_validity = now,
    .state = ApproxAlignerStateType::unspecified,
    .type = AlignmentType::none,
    .alignment =
      Alignment{
        .score = {},
        .inputs = std::apply([](auto&... input) { return InputItTuple(input.get_view().begin()...); }, inputs),
      },
  };
  Aligner::commit(state, inputs, result);

  auto expected_state = State{};
  expected_state.set_last_commit_timestamp(now);
  REQUIRE(expected_state == state);
  auto cursors = std::apply([](auto&... input) { return std::make_tuple(input.get_cursor()...); }, inputs);
  REQUIRE(result.alignment);
  const auto& expected_cursors =
    std::apply([](auto&... itr) { return InputItTuple(std::next(itr)...); }, result.alignment->inputs);
  REQUIRE(expected_cursors == cursors);
}

TEST_CASE_METHOD(TestApproxAlignerFixture, "idle", "[ApproxAligner::find_alignment]")
{
  auto makers = AlignerInputMakers<Aligner, Input0, Input1, Input2>({{.value = 10}}, {{.value = 20}}, {{.value = 30}});
  auto inputs = makers.make_inputs();

  config.set_minimum_wait_time(std::chrono::seconds{1});
  state.set_last_commit_timestamp(SyncTime{std::chrono::seconds{4}});
  const auto max_missing_inputs_on_timeout{GENERATE(0UL, 1UL, 2UL)};
  config.set_max_missing_inputs_on_timeout(max_missing_inputs_on_timeout);

  SECTION("negative duration")
  {
    auto now = SyncTime{std::chrono::seconds{3}};
    auto result = Aligner::find_alignment(resource, config, state, inputs, now);
    REQUIRE(ApproxAlignerStateType::idle == result.state);
  }

  SECTION("less than minimum")
  {
    auto now = SyncTime{std::chrono::seconds{4}};
    auto result = Aligner::find_alignment(resource, config, state, inputs, now);
    REQUIRE(ApproxAlignerStateType::idle == result.state);
  }

  SECTION("equal minimum")
  {
    auto now = SyncTime{std::chrono::seconds{5}};
    auto result = Aligner::find_alignment(resource, config, state, inputs, now);
    REQUIRE(ApproxAlignerStateType::idle != result.state);
  }

  SECTION("greater than minimum")
  {
    auto now = SyncTime{std::chrono::seconds{6}};
    auto result = Aligner::find_alignment(resource, config, state, inputs, now);
    REQUIRE(ApproxAlignerStateType::idle != result.state);
  }
}

TEST_CASE_METHOD(TestApproxAlignerFixture, "invalid_inputs", "[ApproxAligner::find_alignment]")
{
  auto makers = AlignerInputMakers<Aligner, Input0, Input1, Input2>({{.value = 10}}, {{.value = 20}}, {{.value = 30}});
  auto inputs = makers.make_inputs();

  state.set_last_commit_timestamp(SyncTime{std::chrono::seconds{4}});
  auto now = SyncTime{std::chrono::seconds{5}};

  const auto max_missing_inputs_on_timeout{GENERATE(0UL, 1UL, 2UL)};
  config.set_max_missing_inputs_on_timeout(max_missing_inputs_on_timeout);

  SECTION("valid")
  {
    Aligner::Policy::test_validate_inputs_return_ = true;
    auto result = Aligner::find_alignment(resource, config, state, inputs, now);
    REQUIRE(ApproxAlignerStateType::idle != result.state);
    REQUIRE(ApproxAlignerStateType::invalid_inputs != result.state);
  }

  SECTION("invalid")
  {
    Aligner::Policy::test_validate_inputs_return_ = false;
    auto result = Aligner::find_alignment(resource, config, state, inputs, now);
    REQUIRE(ApproxAlignerStateType::invalid_inputs == result.state);
  }
}

TEST_CASE_METHOD(TestApproxAlignerFixture, "timeout", "[ApproxAligner::find_alignment]")
{
  auto makers = AlignerInputMakers<Aligner, Input0, Input1, Input2>({{.value = 10}}, {{.value = 20}}, {{.value = 30}});
  auto inputs = makers.make_inputs();

  config.set_minimum_score_threshold(1);
  config.set_maximum_wait_time(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::seconds{10}).count());
  state.set_last_commit_timestamp(SyncTime{std::chrono::seconds{4}});
  auto now = SyncTime{std::chrono::seconds{15}};

  const auto max_missing_inputs_on_timeout{GENERATE(0UL, 1UL, 2UL)};
  config.set_max_missing_inputs_on_timeout(max_missing_inputs_on_timeout);

  SECTION("no alignment")
  {
    auto result = Aligner::find_alignment(resource, config, state, inputs, now);
    REQUIRE(ApproxAlignerStateType::timeout == result.state);
    REQUIRE(AlignmentType::partial == result.type);
    REQUIRE(result.alignment);
    REQUIRE(static_cast<double>(result.alignment->score) > config.get_minimum_score_threshold());
  }
}

TEST_CASE_METHOD(TestApproxAlignerFixture, "extract", "[ApproxAligner::extract_values]")
{
  auto makers = AlignerInputMakers<Aligner, Input0, Input1, Input2>(
    {{.value = 10}, {.value = 11}}, {}, {{.value = 30}, {.value = 31}, {.value = 32}});
  auto inputs = makers.make_inputs();

  const auto expected = std::array<std::pmr::vector<ValueType>, input_count>{
    std::pmr::vector<ValueType>({10, 11}, resource),
    std::pmr::vector<ValueType>({}, resource),
    std::pmr::vector<ValueType>({30, 31, 32}, resource),
  };

  auto actual = Aligner::extract_values(resource, inputs);
  REQUIRE(expected == actual);
}

TEST_CASE_METHOD(TestApproxAlignerFixture, "full", "[CombinationGenerator]")
{
  const auto max_missing_inputs_on_timeout{GENERATE(0UL, 1UL, 2UL)};

  SECTION("all empty")
  {
    const auto values = ValueVectorsArray{};
    auto generator = detail::CombinationGenerator<ValueType, input_count>(
      jewels::memory::make_non_null_from_ref(values), max_missing_inputs_on_timeout);
    REQUIRE(generator.begin() == generator.end());
  }

  const auto full_values = ValueVectorsArray{
    ValueVector({10, 11}, resource),
    ValueVector({20}, resource),
    ValueVector({30, 31, 32}, resource),
  };

  SECTION("any input empty")
  {
    for (size_t i = 0; i < full_values.size(); ++i)
    {
      auto values = full_values;
      values.at(i).clear();
      auto generator = detail::CombinationGenerator<ValueType, input_count>(
        jewels::memory::make_non_null_from_ref(values), max_missing_inputs_on_timeout);

      if (max_missing_inputs_on_timeout > 0UL)
      {
        REQUIRE_FALSE(generator.begin() == generator.end());
        auto expected_end{generator.begin()};
        for (const auto& value_vec : values)
        {
          for ([[maybe_unused]] const auto value : value_vec)
          {
            ++expected_end;
          }
        }
        ++expected_end;
        REQUIRE(expected_end == generator.end());
      }
      else
      {
        REQUIRE(generator.begin() == generator.end());
      }
    }
  }

  SECTION("all combinations")
  {
    auto values = full_values;
    auto expected_combinations = std::vector<ValuePtrArray>({
      {&values.at(0).at(0), &values.at(1).at(0), &values.at(2).at(0)},
      {&values.at(0).at(0), &values.at(1).at(0), &values.at(2).at(1)},
      {&values.at(0).at(0), &values.at(1).at(0), &values.at(2).at(2)},
      {&values.at(0).at(1), &values.at(1).at(0), &values.at(2).at(0)},
      {&values.at(0).at(1), &values.at(1).at(0), &values.at(2).at(1)},
      {&values.at(0).at(1), &values.at(1).at(0), &values.at(2).at(2)},
    });
    auto expected_indices = std::vector<std::array<ssize_t, input_count>>({
      {0, 0, 0},
      {0, 0, 1},
      {0, 0, 2},
      {1, 0, 0},
      {1, 0, 1},
      {1, 0, 2},
    });

    auto generator = detail::CombinationGenerator<ValueType, input_count>(
      jewels::memory::make_non_null_from_ref(values), max_missing_inputs_on_timeout);

    size_t index = 0;
    for (auto it = generator.begin(); it != generator.end(); ++it)
    {
      REQUIRE(index < expected_combinations.size());
      REQUIRE(expected_combinations.at(index) == it.value_ptrs());
      REQUIRE(expected_indices.at(index) == it.indices());
      ++index;
    }
    REQUIRE(index == expected_combinations.size());
  }

  SECTION("combinations including input 1")
  {
    auto values = full_values;
    values.at(1).clear();
    const auto allow_empty{max_missing_inputs_on_timeout > 0UL};
    const auto expected_combinations = allow_empty
                                         ? std::vector<ValuePtrArray>{
                                             {&values.at(0).at(0), nullptr, &values.at(2).at(0)},
                                             {&values.at(0).at(0), nullptr, &values.at(2).at(1)},
                                             {&values.at(0).at(0), nullptr, &values.at(2).at(2)},
                                             {&values.at(0).at(1), nullptr, &values.at(2).at(0)},
                                             {&values.at(0).at(1), nullptr, &values.at(2).at(1)},
                                             {&values.at(0).at(1), nullptr, &values.at(2).at(2)},
                                           }
                                         : std::vector<ValuePtrArray>{};
    const auto expected_indices = allow_empty ? std::vector<std::array<ssize_t, input_count>>({
                                                  {0, 0, 0},
                                                  {0, 0, 1},
                                                  {0, 0, 2},
                                                  {1, 0, 0},
                                                  {1, 0, 1},
                                                  {1, 0, 2},
                                                })
                                              : std::vector<std::array<ssize_t, input_count>>{};

    auto generator = detail::CombinationGenerator<ValueType, input_count>(
      jewels::memory::make_non_null_from_ref(values), max_missing_inputs_on_timeout);

    size_t index = 0;
    for (auto it = generator.begin(); it != generator.end(); ++it)
    {
      REQUIRE(index < expected_combinations.size());
      REQUIRE(expected_combinations.at(index) == it.value_ptrs());
      REQUIRE(expected_indices.at(index) == it.indices());
      ++index;
    }
    REQUIRE(index == expected_combinations.size());
  }
  SECTION("combinations including input 0")
  {
    auto values = full_values;
    values.at(0).clear();
    const auto allow_empty{max_missing_inputs_on_timeout > 0UL};
    const auto expected_combinations = allow_empty
                                         ? std::vector<ValuePtrArray>{
                                             {nullptr, &values.at(1).at(0), &values.at(2).at(0)},
                                             {nullptr, &values.at(1).at(0), &values.at(2).at(1)},
                                             {nullptr, &values.at(1).at(0), &values.at(2).at(2)},
                                           } : std::vector<ValuePtrArray>{};
    const auto expected_indices = allow_empty ? std::vector<std::array<ssize_t, input_count>>({
                                                  {0, 0, 0},
                                                  {0, 0, 1},
                                                  {0, 0, 2},
                                                })
                                              : std::vector<std::array<ssize_t, input_count>>{};

    auto generator = detail::CombinationGenerator<ValueType, input_count>(
      jewels::memory::make_non_null_from_ref(values), max_missing_inputs_on_timeout);

    size_t index = 0;
    for (auto it = generator.begin(); it != generator.end(); ++it)
    {
      REQUIRE(index < expected_combinations.size());
      REQUIRE(expected_combinations.at(index) == it.value_ptrs());
      REQUIRE(expected_indices.at(index) == it.indices());
      ++index;
    }
    REQUIRE(index == expected_combinations.size());
  }
}

TEST_CASE_METHOD(TestApproxAlignerFixture, "full", "[ApproxAligner::find_full_alignment]")
{
  const auto max_missing_inputs_on_timeout{GENERATE(0UL, 1UL)};
  config.set_max_missing_inputs_on_timeout(max_missing_inputs_on_timeout);

  SECTION("all empty")
  {
    auto makers = AlignerInputMakers<Aligner, Input0, Input1, Input2>({}, {}, {});
    auto inputs = makers.make_inputs();
    auto values = Aligner::extract_values(resource, inputs);

    auto [full_alignment, timeout_alignment] = Aligner::find_full_alignment(config, inputs, values);
    REQUIRE_FALSE(full_alignment);
    REQUIRE_FALSE(timeout_alignment);
  }

  SECTION("any empty")
  {
    auto makers =
      AlignerInputMakers<Aligner, Input0, Input1, Input2>({{.value = 10}}, {{.value = 20}}, {{.value = 30}});
    auto inputs = makers.make_inputs();
    auto values = Aligner::extract_values(resource, inputs);

    {
      auto [full_alignment, timeout_alignment] = Aligner::find_full_alignment(config, inputs, values);
      REQUIRE(full_alignment);
      REQUIRE_FALSE(timeout_alignment);
      REQUIRE(static_cast<double>(full_alignment->score) <= config.get_minimum_score_threshold());
    }

    for (size_t i = 0; i < input_count; ++i)
    {
      auto modified_values = values;
      modified_values.at(i).clear();

      auto modified_inputs = inputs;
      auto expected_result_0{std::get<0UL>(modified_inputs).get_cursor_view().begin()};
      auto expected_result_1{std::get<1UL>(modified_inputs).get_cursor_view().begin()};
      auto expected_result_2{std::get<2UL>(modified_inputs).get_cursor_view().begin()};
      switch (i)
      {
      case 0UL:
        std::get<0UL>(modified_inputs).set_cursor(std::get<0UL>(modified_inputs).get_view().end());
        expected_result_0 = std::get<0UL>(modified_inputs).get_cursor_view().end();
        break;
      case 1UL:
        std::get<1UL>(modified_inputs).set_cursor(std::get<1UL>(modified_inputs).get_view().end());
        expected_result_1 = std::get<1UL>(modified_inputs).get_cursor_view().end();
        break;
      case 2UL:
        std::get<2UL>(modified_inputs).set_cursor(std::get<2UL>(modified_inputs).get_view().end());
        expected_result_2 = std::get<2UL>(modified_inputs).get_cursor_view().end();
        break;
      default:
        // If this fails, test inputs have been added. Add corresponding branches to this switch statement,
        // and the corresponding REQUIRE checks at the end of this SECTION.
        REQUIRE(false);
        break;
      }

      auto [full_alignment, timeout_alignment] = Aligner::find_full_alignment(config, modified_inputs, modified_values);
      REQUIRE_FALSE(full_alignment);
      if (max_missing_inputs_on_timeout == 0UL)
      {
        REQUIRE_FALSE(timeout_alignment);
        continue;
      }
      REQUIRE(timeout_alignment);
      REQUIRE(static_cast<double>(timeout_alignment->score) > config.get_minimum_score_threshold());
      REQUIRE(std::get<0UL>(timeout_alignment->inputs) == expected_result_0);
      REQUIRE(std::get<1UL>(timeout_alignment->inputs) == expected_result_1);
      REQUIRE(std::get<2UL>(timeout_alignment->inputs) == expected_result_2);
    }
  }

  SECTION("find first vs last")
  {
    auto makers = AlignerInputMakers<Aligner, Input0, Input1, Input2>(
      {{.value = 10}, {.value = 11}}, {{.value = 20}, {.value = 22}}, {{.value = 30}, {.value = 33}});
    auto inputs = makers.make_inputs();
    auto values = Aligner::extract_values(resource, inputs);

    auto expected_first = std::apply([](auto&... input) { return InputItTuple(input.get_cursor()...); }, inputs);
    auto expected_last =
      std::apply([](auto&... input) { return InputItTuple(std::next(input.get_cursor())...); }, inputs);

    config.set_find_type(ApproxAlignerFindType::first);
    auto [first_full_alignment, first_timeout_alignment] = Aligner::find_full_alignment(config, inputs, values);
    REQUIRE(first_full_alignment);
    REQUIRE(expected_first == first_full_alignment->inputs);

    config.set_find_type(ApproxAlignerFindType::last);
    auto [last_full_alignment, last_timeout_alignment] = Aligner::find_full_alignment(config, inputs, values);
    REQUIRE(last_full_alignment);
    REQUIRE(expected_last == last_full_alignment->inputs);
  }

  SECTION("no alignment if score is gt threshold")
  {
    auto makers =
      AlignerInputMakers<Aligner, Input0, Input1, Input2>({{.value = 10}}, {{.value = 20}}, {{.value = 30}});
    auto inputs = makers.make_inputs();
    auto values = Aligner::extract_values(resource, inputs);

    auto [full_alignment, timeout_alignment] = Aligner::find_full_alignment(config, inputs, values);
    REQUIRE(full_alignment);
    REQUIRE_FALSE(timeout_alignment);

    config.set_minimum_score_threshold(0);
    std::tie(full_alignment, timeout_alignment) = Aligner::find_full_alignment(config, inputs, values);
    REQUIRE_FALSE(full_alignment);
    REQUIRE(timeout_alignment);
  }

  SECTION("no alignment starts at cursors")
  {
    auto makers = AlignerInputMakers<Aligner, Input0, Input1, Input2>(
      {{.value = 10}, {.value = 11}}, {{.value = 20}, {.value = 22}}, {{.value = 30}, {.value = 33}});
    auto inputs = makers.make_inputs();

    auto& input0 = std::get<0>(inputs);
    input0.set_cursor(std::next(input0.get_cursor()));

    auto& input1 = std::get<1>(inputs);
    input1.set_cursor(std::next(input1.get_cursor()));

    auto& input2 = std::get<2>(inputs);

    auto values = Aligner::extract_values(resource, inputs);
    REQUIRE(values.at(0UL).size() == 1UL);
    REQUIRE(values.at(1UL).size() == 1UL);
    REQUIRE(values.at(2UL).size() == 2UL);

    const auto first_score =
      Aligner::Policy::objective({&values.at(0UL).at(0), &values.at(1UL).at(0), &values.at(2UL).at(0)});
    const auto second_score =
      Aligner::Policy::objective({&values.at(0UL).at(0), &values.at(1UL).at(0), &values.at(2UL).at(1)});
    REQUIRE(first_score == second_score);

    auto [full_alignment, timeout_alignment] = Aligner::find_full_alignment(config, inputs, values);
    REQUIRE(full_alignment);
    // No timeout alignment since both candidate combinations have the same score per the test policy,
    // which is under the configured threshold.
    REQUIRE_FALSE(timeout_alignment);

    auto expected = Alignment{
      .score = 4,
      .inputs = InputItTuple(input0.get_cursor(), input1.get_cursor(), input2.get_cursor()),
    };
    REQUIRE(expected == *full_alignment);
  }
}

} // namespace
} // namespace clockwork
