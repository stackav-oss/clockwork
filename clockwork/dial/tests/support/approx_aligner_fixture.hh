// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "clockwork/dial/approx_aligner.hh"
#include "clockwork/dial/approx_aligner_config.hh"
#include "clockwork/dial/approx_aligner_policies.hh"
#include "clockwork/dial/msg_input.hh"
#include "jewels/container/circular_buffer.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"

#include <boost/iterator/iterator_facade.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <memory_resource>
#include <optional>
#include <span>
#include <sys/types.h>
#include <tuple>
#include <utility>
#include <vector>

namespace clockwork
{

struct TestMsg0
{
  int64_t value = {};
  bool operator==(const TestMsg0&) const = default;
};

struct TestMsg1
{
  int64_t value = {};
  bool operator==(const TestMsg1&) const = default;
};

struct TestMsg2
{
  int64_t value = {};
  bool operator==(const TestMsg2&) const = default;
};

struct Input0
{
  using MsgType = TestMsg0;
  using ValueType = int64_t;
  static constexpr auto max_msgs = 10;

  static constexpr ValueType get_value(const MsgType& msg)
  {
    return msg.value;
  };
};

struct Input1
{
  using MsgType = TestMsg1;
  using ValueType = int64_t;
  static constexpr auto max_msgs = 10;

  static constexpr ValueType get_value(const MsgType& msg)
  {
    return msg.value;
  };
};

struct Input2
{
  using MsgType = TestMsg2;
  using ValueType = int64_t;
  static constexpr auto max_msgs = 10;

  static constexpr ValueType get_value(const MsgType& msg)
  {
    return msg.value;
  };
};

struct DoubleMsg
{
  double value = {};
};

struct DoubleInput
{
  using MsgType = DoubleMsg;
  using ValueType = double;
  static constexpr auto max_msgs = 10;

  static constexpr ValueType get_value(const MsgType& msg)
  {
    return msg.value;
  };
};

template <typename InputPolicy>
class MsgDialInputMaker
{
public:
  using MsgType = typename InputPolicy::MsgType;
  static constexpr auto max_msgs = InputPolicy::max_msgs;
  using InputType = MessageInputDialWithCursorControl<MsgType, max_msgs>;

  MsgDialInputMaker()
    : MsgDialInputMaker({})
  {
  }

  MsgDialInputMaker(std::initializer_list<MsgType> msgs)
    : msgs_(msgs.begin(), msgs.end()), buffer_{std::in_place, std::span(storage_)}
  {
    for (const auto& msg : msgs_)
    {
      buffer_.force_emplace_back(&msg);
    }
  }

  auto make_input()
  {
    const ViewType view{buffer_};
    auto begin = view.begin();
    return InputType(view, begin, begin);
  }

private:
  using CircularBuffer =
    jewels::container::CircularBuffer<detail::MsgPolicy<MsgType>, std::span<const MsgType*, max_msgs>>;
  using ViewType = typename InputType::ViewType;

  std::vector<MsgType> msgs_;
  std::array<const MsgType*, max_msgs> storage_;
  CircularBuffer buffer_;
};

template <typename AlignerType, typename... InputPolicies>
class AlignerInputMakers
{
public:
  using Aligner = AlignerType;
  using ValueType = typename Aligner::ValueType;
  template <typename MsgType>
  using MsgInitList = std::initializer_list<MsgType>;
  using AlignerInputTuple = typename Aligner::InputTuple;

  template <typename InputPolicy>
  using Maker = MsgDialInputMaker<InputPolicy>;
  using MakerTuple = std::tuple<Maker<InputPolicies>...>;
  template <typename InputPolicy>
  using InputType = typename Maker<InputPolicy>::InputType;
  using InputsTuple = std::tuple<InputType<InputPolicies>...>;

  AlignerInputMakers()
    : inputs_(std::apply([](auto&... maker) { return InputsTuple(maker.make_input()...); }, makers_))
  {
  }

  explicit AlignerInputMakers(MsgInitList<typename InputPolicies::MsgType>... values)
    : makers_(values...),
      inputs_(std::apply([](auto&... maker) { return InputsTuple(maker.make_input()...); }, makers_))
  {
  }

  auto make_inputs()
  {
    return std::apply([](auto&... input) { return AlignerInputTuple(input...); }, inputs_);
  }

private:
  MakerTuple makers_;
  InputsTuple inputs_;
};

template <typename... InputPolicies>
struct TestAlignerPolicy
{
  static_assert(all_same_v<typename InputPolicies::ValueType...>, "All input value types must be the same.");

  static constexpr auto input_count = sizeof...(InputPolicies);

  using ValueType = typename std::tuple_element_t<0, std::tuple<InputPolicies...>>::ValueType;
  using ValuePtrArray = std::array<const ValueType*, input_count>;
  using ValueVector = std::pmr::vector<ValueType>;
  template <typename InputPolicy>
  using InputType = MessageInputDialWithCursorControl<typename InputPolicy::MsgType, InputPolicy::max_msgs>;
  using InputTuple = std::tuple<InputType<InputPolicies>&...>;
  using InputItTuple = std::tuple<typename InputType<InputPolicies>::IteratorType...>;
  using IndexArray = std::array<ssize_t, input_count>;

  /// Test variable used to indicate if inputs are valid.
  static bool test_validate_inputs_return_;

  /// Test validation function.
  static constexpr bool validate_inputs(const InputTuple& /*inputs*/)
  {
    return test_validate_inputs_return_;
  }

  /// Test less than function.
  static constexpr bool less_than(const IndexArray& lhs, const IndexArray& rhs)
  {
    return lhs < rhs;
  }

  /// Test variable to store the objective function return value.
  static ValueType test_objective_score_return_;

  /// Test alignment function.
  static constexpr ValueType objective(const ValuePtrArray& /*values*/)
  {
    return test_objective_score_return_;
  }
};

template <typename... InputTypes>
bool TestAlignerPolicy<InputTypes...>::test_validate_inputs_return_ = {};

template <typename... InputTypes>
TestAlignerPolicy<InputTypes...>::ValueType TestAlignerPolicy<InputTypes...>::test_objective_score_return_ = {};

template <typename AlignerType>
struct ApproxAlignerFixture
{
  using Aligner = AlignerType;

  static constexpr auto input_count = Aligner::input_count;
  using Alignment = typename Aligner::Alignment;
  using Config = typename Aligner::Config;
  using Result = typename Aligner::Result;
  using State = typename Aligner::State;
  using SyncTime = typename Aligner::SyncTime;
  using ValueType = typename Aligner::ValueType;
  using ValueVector = typename Aligner::ValueVector;
  using ValuePtrArray = typename Aligner::ValuePtrArray;
  using ValueVectorsArray = typename Aligner::ValueVectorsArray;
  using InputTuple = typename Aligner::InputTuple;
  using InputItTuple = typename Aligner::InputItTuple;
  using IndexArray = typename Aligner::IndexArray;

  jewels::memory::MemoryResource resource{std::pmr::new_delete_resource()};
  Config config = {};
  State state = {};
};

using TestAlignerType = ApproxAligner<TestAlignerPolicy, Input0, Input1, Input2>;

struct TestApproxAlignerFixture : public ApproxAlignerFixture<TestAlignerType>
{
  using Aligner = TestAlignerType;

  static constexpr auto input_count = Aligner::input_count;
  using Alignment = typename Aligner::Alignment;
  using Config = typename Aligner::Config;
  using Result = typename Aligner::Result;
  using State = typename Aligner::State;
  using SyncTime = typename Aligner::SyncTime;
  using ValueType = typename Aligner::ValueType;
  using ValueVector = typename Aligner::ValueVector;
  using ValuePtrArray = typename Aligner::ValuePtrArray;
  using ValueVectorsArray = typename Aligner::ValueVectorsArray;
  using InputTuple = typename Aligner::InputTuple;
  using InputItTuple = typename Aligner::InputItTuple;
  using IndexArray = typename Aligner::IndexArray;

  TestApproxAlignerFixture()
  {
    Aligner::Policy::test_validate_inputs_return_ = true;
    Aligner::Policy::test_objective_score_return_ = 4;
    config.set_minimum_score_threshold(10);
  }
};

using DoubleAlignerPolicies = ApproxAlignerPolicies<DoubleInput, DoubleInput, DoubleInput>;

template <typename... InputPolicies>
struct DoubleAlignerPolicy : ApproxAlignerPolicies<InputPolicies...>
{
  static constexpr auto validate_inputs = ApproxAlignerPolicies<InputPolicies...>::always_valid_inputs;
  static constexpr auto less_than = ApproxAlignerPolicies<InputPolicies...>::array_less_than;
  static constexpr auto objective = ApproxAlignerPolicies<InputPolicies...>::variance_objective;
};

using DoubleAlignerType = ApproxAligner<DoubleAlignerPolicy, DoubleInput, DoubleInput, DoubleInput>;
using DoubleTestApproxAlignerFixture = ApproxAlignerFixture<DoubleAlignerType>;

} // namespace clockwork
