# Stream aligners

Clockwork provides a set of user libraries that can align data from multiple streams, or channels, with customizable alignment criteria.
These libraries can be instantiated directly in the Cog user code.
This document describes the available libraries and how to use them.

## ApproxAligner

The ApproxAligner library provides a generic interface for finding sets of inputs based on user defined function.

### High-level approach

#### Problem definition

Given a set of $N$ input vectors, each with $0-M_n$ messages, find the set of message indices $(i_0, i_1, …)$ that minimizes an objective function $`f(key_0(N_0[i_0]), key_1(N_1[i_1]), …)`$.

`f()` – function that computes a score to represent how well aligned the input set is, where the lower the score the better the alignment.
There are no requirements for the function besides it must return a real number (i.e. not NaN).

$`key_n(N_n[i])`$ – extracts the value from the input for the objective function (e.g. index, time of validity, etc)

#### Assumptions

The terminology used in the following sections matches that used for a FIFO style container.
In the context of Clockwork, assuming there is a single publisher, first message is the oldest (by time) message and the last is the most recent message.

1. The aligner will never rearrange the inputs. That is, it is assumed that the inputs are already in the desired order. There is a validation function that can be used to ensure this at runtime.
2. Messages will never be reused. Once a message is considered part of an aligned set it will not be considered as part of a new aligned set.
3. The alignment will never go backwards. The algorithm will only search forward from the last used message in each input.
4. The aligner will never copy the inputs, it utilizes the Clockwork Dial inputs to track which inputs have already been used.
5. Aligned sets can only consist of one (or zero) messages from each input. A full set contains one message from each input while a partial set can be missing messages for one or more input.
6. The alignment will break ties using the policy `less_than` operator in conjunction with the `find_type` option.

#### Configuration

1. Find type - Which element to select when breaking ties.
   - `ApproxAlignerFindType::first` \- The first element in the sorted output. This is the default behavior.
   - `ApproxAlignerFindType::last` \- The last element in the sorted output.
2. Minimum score threshold – only consider a set aligned if its score is below the threshold.
   - If multiple sets are found that satisfy the minimum threshold they will be ordered using the policy `less_than` function.
3. Minimum wait duration – only look for sets after a minimum amount of time has passed since the last alignment was accepted. This is to allow for comms latency between channels.
   - This is not a blocking call. The algorithm will just do nothing until the time the delta between when the function was called (using cog start execution time) and the last alignment time exceeds the minimum.
4. Maximum wait duration (optional) – if no set satisfies the minimum threshold then return the best, by objective score, set of all the other inputs.
   - If maximum wait time is reached it will consider the current best set as aligned even if it does not meet the minimum score threshold.

### User interface

The ApproxAligner library provides a static interface that is configured using templated policy structs.

#### Input policy

The input policy structs are used to specify the input message type and the function to extract the key values from that message type.

The following is an example of extracting the time of validity, in seconds, from a notional `AlignerTestMessage`.

```cpp
struct DeltaPosePolicy
{
  /// Underlying message type.
  using MsgType = Tappy<clockwork::testing::AlignerTestMessage>;
  /// Type used for the extracted value from message.
  using ValueType = double;
  /// The max size of the input view.
  static constexpr auto max_msgs = 100;

  /// Function used to extract the value used for alignment from the message.
  static constexpr ValueType get_value(const MsgType& msg)
  {
    auto tov = msg.get_time_of_validity().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::duration<double>>(tov).count();
  }
};
```

#### `AlignerPolicy`

The `AlignerPolicy` struct is used to specify the input validation method, the objective function, and less than ordering.

```cpp
template <typename... InputPolicies>
struct AlignerPolicy
{
  static constexpr auto input_count = sizeof...(InputPolicies);
  using ValueType = typename std::tuple_element_t<0, std::tuple<InputPolicies...>>::ValueType;
  using ValuePtrsArray = std::array<const ValueType*, input_count>;

  template <typename InputPolicy>
  using InputType = MessageInputDialWithCursorControl<typename InputPolicy::MsgType, InputPolicy::max_msgs>;
  using InputTuple = std::tuple<InputType<InputPolicies>&...>;
  using InputItTuple = std::tuple<typename InputType<InputPolicies>::IteratorType...>;
  using IndexArray = std::array<ssize_t, input_count>;

  /// Customizable validate inputs function.
  /// @param[in] inputs Set of input begin/end iterators to validate.
  /// @return True is the inputs are valid, false otherwise.
  static constexpr bool validate_inputs(const InputTuple & inputs);

  /// Customizable less than operator for input sets.
  /// @param[in] lhs The array of indices on the left hand side of the operator.
  /// @param[in] rhs The array of indices on the right hand side of the operator.
  /// @return true if the lhs set is less than the rhs set.
  static constexpr bool less_than(const IndexArray &lhs, const IndexArray &rhs);

  /// Customizable alignment objective function to minimize.
  /// @param[in] values Array of input values to evaluate.
  /// @return Objective function score. Lower the value the better the alignment.
  static constexpr ValueType objective(const std::array<const ValueType *, input_count> &values);
};
```

#### Common policies

Clockwork provides some commonly used policy structs that can be shared across users.

The `clockwork/dial/approx_aligner_policies.hh` module provides the common policy definitions.

##### Provided policies

Input policies:

- `TovNanosecondsApproxAlignerInput` - extracts a `time_of_validity` field as int64 nanoseconds.
- `TovSecondsApproxAlignerInput` - extracts a `time_of_validity` field as double seconds.

Validation functions:

- `always_valid_inputs` - just return true.
- `monotonically_increasing_inputs` - check that inputs are monotonically increasing.

Less Than funtions:

- `array_less_than` - less than operator that uses the default array less than operator.

Objective functions:

- `exact_alignment_objective` - compute exact alignment score. Where the score ranges from `[0 - all aligned, input_count - no inputs]`.
- `variance_objective` - compute approx alignment score based on input variance.

#### Usage

Below is an example of using the common policies to find left and right images with the same time of validity for the Cog, `MyCog`.

Add the approx aligner config to your Cog:

```clockwork
configs
{
    aligner_config: Tappy<approx_aligner_config::ApproxAlignerConfig>;
}
```

Add the approx aligner state to your Cog (be sure to make it mutable):

```clockwork
states
{
    aligner_state: Tappy<approx_aligner_config::ApproxAlignerState>
    {
        mutable: true;
    }
}
```

Add your Cog inputs with manual cursors.
Remember the aligner will not copy/store any inputs so the size of the input view is how much history it will have available to find aligned sets.

```clockwork
inputs
{
    left_image: Tappy<AlignerTestMsg>
    {
      max_msgs: 10;
      manual_cursor: true;
    }

    right_image: Tappy<AlignerTestMsg>
    {
      max_msgs: 10;
      manual_cursor: true;
    }
}
```

Define the `ApproxAligner` policies.

```cpp
template <typename... InputPolicies>
struct AlignImagesPolicy : ApproxAlignerPolicies<InputPolicies...>
{
  static constexpr auto validate_inputs = ApproxAlignerPolicies<InputPolicies...>::monotonically_increasing_inputs;
  static constexpr auto less_than = ApproxAlignerPolicies<InputPolicies...>::array_less_than;
  static constexpr auto objective = ApproxAlignerPolicies<InputPolicies...>::exact_alignment_objective;
};

using LeftImageInput = TovNanosecondsApproxAlignerInput<std::decay_t<decltype(MyCogCogDial::Inputs::left_image)>>;
using RightImageInput = TovNanosecondsApproxAlignerInput<std::decay_t<decltype(MyCogCogDial::Inputs::right_image)>>;
using Aligner = ApproxAligner<AlignImagesPolicy, IntInput, FloatInput>;
```

Call the aligner in your Cog execute function.

```cpp
const auto& now = dial.start_time;
const auto& resource = dial.resources.aligner_resource;
const auto& config = dial.configs.aligner_config;
auto& state = *dial.states.aligner_state;
auto inputs = Aligner::InputTuple(dial.inputs.left_image, dial.inputs.right_image);

// Call the aligner.

auto result = Aligner::find_alignment(resource, config, state, inputs, now);

// Handle the result accordingly...
```

Update the state object if the alignment was accepted.

```cpp
Aligner::commit(state, inputs, result);
```

#### Example

For a full working example see `ApproxAlignerCog` in `clockwork/examples/approx_aligner/approx_aligner_cog.clk`.
