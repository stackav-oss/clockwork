# Callsig - Call Signature Library

The `callsig` jewel provides facilities related to defining call signatures and handling function outcomes in a safe manner.
It implements a particular opinionated approach to function interfaces, which uses return values to indicate an outcome (success, failure, or other enumerated states) rather than exceptions or other mechanisms, and uses output parameters to produce results (rather than returning values).

`BinaryOutcome` and `Outcome` are the return types from functions to indicate outcomes (success, failure, or other enumerated states).
These classes enforce explicit checking of return values and provide compile-time safety for handling different outcomes.

The library also provides explicit output parameter types (`Out`, `OptionalOut`, `MaybeOut`) that make the intent of parameters clear at both function definition and call sites.

## Classes

### BinaryOutcome

`BinaryOutcome` is designed for simple success/failure scenarios without other distinct outcomes.

#### Features

- No implicit conversion to `bool` - explicit checking required (primarily via `ok()` or `fails()` free functions)
- Marked `[[nodiscard]]` to prevent ignoring return values (do not redundantly declare functions `[[nodiscard]]`)
- Constexpr instances for easy return values
- Zero-overhead implementation

#### Usage

```cpp
#include "jewels/callsig/outcome.hh"

// Optional for reducing `jewels::` verbosity
using jewels::success;
using jewels::failure;
using jewels::ok;
using jewels::fails;

jewels::BinaryOutcome example_function()
{
  if (some_condition) {
    return success;
  }
  return failure;
}

void caller()
{
  // Direct checking in if statement (preferred)
  if (ok(example_function())) {
    // Handle success
  }

  // Or store and check later
  const auto outcome = example_function();
  if (fails(outcome)) {
    return failure;
  }
}
```

### Outcome<T, success_values...>

`Outcome<T>` is designed for functions that need to provide an enumerated set of outcomes beyond simple success/failure.
It can optionally specify which enum values are considered successful, allowing for both simple success checking and detailed outcome handling.

#### Features

- Marked `[[nodiscard]]` to prevent ignoring return values (do not redundantly declare functions `[[nodiscard]]`)
- Template parameter `T` must be an enum type
- Optional template parameters `success_values...` define which enum values are considered successful
- Implicit construction from enum values
- When success values are defined, the same `ok()` and `fails()` free functions can be used for simple success/failure checking

#### Usage

##### Basic enumerated outcomes

```cpp
enum class ProcessingResult
{
  completed,
  timeout,
  invalid_input,
  computation_error
};

jewels::Outcome<ProcessingResult> process_data(const Data& input)
{
  // Implementation details...
  return ProcessingResult::completed; // Implicit conversion from enum; no boilerplate needed
}

void caller()
{
  // Must use switch/case to handle all outcomes
  switch (process_data(input)) {
    case ProcessingResult::completed:
      // Handle success
      break;
    case ProcessingResult::timeout:
      // Handle timeout
      break;
    case ProcessingResult::invalid_input:
      // Handle invalid input
      break;
    case ProcessingResult::computation_error:
      // Handle error
      break;
  }
}
```

##### With success values for simplified checking

```cpp
enum class NetworkResult
{
  success,
  partial_success,
  timeout,
  connection_failed
};

// Define which values are considered successful
using NetworkOutcome = jewels::Outcome<NetworkResult,
                                       NetworkResult::success,
                                       NetworkResult::partial_success>;

NetworkOutcome send_data(const Data& data)
{
  // Implementation details...
  return NetworkResult::success;
}

void caller()
{
  const auto result = send_data(data);

  // Can use ok()/fails() for simple success/failure checking
  if (jewels::ok(result)) {
    // Handle any successful outcome
    return;
  }

  // Or handle specific outcomes
  switch (result.get()) {
    case NetworkResult::success:
    case NetworkResult::partial_success:
      // Already handled above
      break;
    case NetworkResult::timeout:
      // Handle timeout
      break;
    case NetworkResult::connection_failed:
      // Handle connection failure
      break;
  }
}
```

## Free Functions

### ok() and fails()

Free function versions are provided for consistent interface across outcome types:

```cpp
template <OutcomeType T>
[[nodiscard]] constexpr bool ok(const T& outcome) noexcept;

template <OutcomeType T>
[[nodiscard]] constexpr bool fails(const T& outcome) noexcept;
```

### outcome_in()

Check if an outcome's value matches any of the provided enum values:

```cpp
if (jewels::outcome_in<NetworkResult::timeout,
                       NetworkResult::connection_failed>(result)) {
  // Handle network errors
}
```

## Usage Guidance

### For Callers

#### Use appropriate checking method

- For `BinaryOutcome`: Use `ok()` or `fails()` in `if` statements
- For `Outcome<T>` with success values: Can use `ok()`/`fails()` for simple checking
- For `Outcome<T>` without success values: Generally use `switch/case` on `get()`

If the returned outcome type provides `get()` and the caller is using it to handle each enumerated outcome, then the caller must[^1] use a switch/case statement to check the outcome, as shown above.
This is essential as it allows the compiler to ensure that every outcome is considered and explicitly handled.

[^1]: Unit tests may compare to specific values instead.

```cpp
switch (result.get()) {
    case MyEnum::case1:
    // handle case1
    break;
    case MyEnum::case2:
    // handle case2
    break;
    // Compiler will warn if cases are missing
}
```

If you want to define your own set of success values, you may use `outcome_in()` instead of a switch/case statement.

#### Prefer direct checking in `if` statements

```cpp
// Preferred
if (jewels::fails(some_function())) {
    return failure;
}

// Only when you need to reference the outcome later
const auto outcome = some_function();
if (jewels::fails(outcome)) {
    log_error("Function failed with: ", outcome.get());
    return failure;
}
```

### For Function Designers

Function designers need to consider their **outcome type to be part of their API contract**.
This is the main determining factor for deciding between the different outcome types presented in this proposal.
If you return an enumerated outcome, that enumeration is part of your API and changing it may break callers.

#### If your caller needs to respond to different types of failure, use `Outcome<T>`

Some types of functions can have different degrees of success or failure and most callers need to handle each outcome separately.
This is when you should use `Outcome<T>`.

#### If your caller should not distinguish types of failure, use `BinaryOutcome`

If in general callers cannot or should not respond differently to different enumerated outcomes, then simply return success or failure.

#### If some callers need enumerated outcomes but success is clearly defined, use `Outcome<T, …>`

Sometimes the one-size-fits-all solution leads to overly verbose code for callers that don’t need full enumeration.
In that case, and _if and only if_ the enumerated outcomes have a very clear division into success/failure outcomes, then use the full `Outcome<T, …>` type, which allows the caller to decide what they need.

**Do not use this variation if the semantics of success/failure are unclear** in your specific situation, or if you do not want the different enumerated outcomes to be part of your public API.

#### For unit testing, consider a wrapper function

If the only reason you are considering using an enumerated outcome is to make unit testing easier, asserting in the unit test that different paths were chosen by the function under test, consider splitting your function into a private (or pseudo-private) function that returns `Outcome<T, …>` and a public wrapper that converts this trivially to `BinaryOutcome`.
This prevents callers from coming to depend on the implementation details, while still allowing you to test the function's behavior.

## Type Traits and Concepts

The library provides type traits and concepts for generic programming:

- `EnumType<T>` - Concept for enum types
- `OutcomeType<T>` - Concept for outcome types
- `IsOutcomeType<T>` - Type trait for outcome types
- `is_outcome_type_v<T>` - Variable template for outcome type checking

These enable writing generic functions that work with any outcome type.

## Output Parameters

The library provides three wrapper classes for output parameters that make the intent explicit at both function definition and call sites.
These eliminate ambiguity about which parameters are inputs versus outputs.

### `Out<T>`

`Out<T>` is for standard output parameters that are always provided by the caller and always produced by the callee (except on function failure).

#### Features

- Lightweight wrapper around a non-null reference
- Zero runtime overhead
- Explicit signaling of output intent

#### Usage

```cpp
#include "jewels/callsig/outparam.hh"

// Function definition
jewels::BinaryOutcome get_data(jewels::Out<Data> data_out, const Config& config)
{
  data_out->field1 = 42;
  data_out->field2 = "result";
  return jewels::success;
}

// Call site
Data result;
if (fails(get_data(Out{result}, config))) {
  return failure;
}
// result now contains the data
```

### `InOut<T>`

`InOut<T>` is for parameters that act as both inputs and outputs (think mutable state).
As inputs, they are considered mandatory - they are always provided by the caller.
As outputs, they are considered mandatory - the callee will always populate them with a sensible value (including the case where the outputs happen to be the same as the inputs, and the callee has not changed them).

#### Features

- Lightweight wrapper around a non-null reference
- Zero runtime overhead
- Explicit signaling of stateful intent

#### Usage

```cpp
#include "jewels/callsig/outparam.hh"

// Function definition
jewels::BinaryOutcome get_data(jewels::InOut<Data> data_out, const Config& config)
{
  if (data_out->field1 > 42)
  {
    data_out->field2 = "high";
  }
  else
  {
    data_out->field2 = "low";
  }

  return jewels::success;
}

// Call site
Data state;
state.field1 = 20;
if (fails(get_data(InOut{state}, config))) {
  // In the case of failure, state may or may not have been mutated in place.
  // The recommendation is to document in the the function API whether the state is changed on failure.
  return failure;
}
// in the case of success, state has been mutated in-place
```

### `OptionalOut<T>`

`OptionalOut<T>` is for caller-optional output parameters where the caller decides whether the output is needed.

#### Features

- Can be constructed from a reference (output desired) or `std::nullopt` (output not needed)
- Allows avoiding construction of unnecessary output objects
- Provides checked access methods

#### Usage

```cpp
// Function definition
jewels::BinaryOutcome process_data(int id, jewels::OptionalOut<ProcessedData> result_opt_out)
{
  if (result_opt_out.has_value()) {
    // Safe to fill in the output
    result_opt_out->status = "processed";
    result_opt_out->value = id * 2;
  }

  // Do other required work regardless of output
  log_processing(id);
  return success;
}

// Call site when output is desired
ProcessedData result;
if (fails(process_data(42, OptionalOut{result}))) {
  return failure;
}
// result contains the processed data

// Call site when output is not needed
if (fails(process_data(42, std::nullopt))) {
  return failure;
}
// No result object needed
```

### `MaybeOut<T>`

`MaybeOut<T>` is for callee-optional output parameters where the caller always provides an object but the callee decides whether to populate it.

#### Features

- Tracks whether the output was actually produced
- Auto-marks as valid when accessed via non-const operators
- Explicit validity control with `mark_valid()` and `mark_invalid()`

#### Usage

```cpp
// Function definition
jewels::BinaryOutcome try_compute_derived(MaybeOut<DerivedData> derived_out, const SourceData& source)
{
  if (source.has_required_fields()) {
    derived_out->computed_value = source.base_value * 1.5;
    derived_out->timestamp = get_current_time();
    // Auto-marked as valid by accessing via ->
  } else {
    derived_out.mark_invalid();
  }

  // Always do other work
  update_statistics();
  return success;
}

// Call site
DerivedData derived;
jewels::MaybeOut maybe_derived{derived};
if (fails(try_compute_derived(maybe_derived, source_data))) {
  return failure;
}

if (maybe_derived.is_valid()) {
  use_derived_data(derived);
} else {
  // derived state is indeterminate, use fallback
  use_fallback_approach();
}
```

### `FactoryResult<T>` and `FactoryOut<T>`

These classes are for outputs of factory functions of non-default-constructible types (or when default construction is expensive and worth avoiding).
`FactoryResult<T>` is the result container, used for declaring the result variable at the caller with deferred construction.
`FactoryOut<T>` is the form used for declaring the function that takes the parameter, and is just a convenience alias for `Out<FactoryResult<T>>`.

#### Usage

```cpp
class SensorCalibration
{
  Eigen::Matrix3d rotation_matrix_;
  Eigen::Vector3d translation_vector_;
  double scale_factor_;
public:
  // No public default constructor
  SensorCalibration(const Eigen::Matrix3d& rotation,
                   const Eigen::Vector3d& translation,
                   double scale)
    : rotation_matrix_(rotation), translation_vector_(translation), scale_factor_(scale) {}

  PointCloud apply_calibration(const PointCloud& cloud) const;
  // ... other methods
};

/// Function definition using FactoryOut
/// @post If the function succeeds, then @param(calibration_out) will contain a valid SensorCalibration object.
jewels::BinaryOutcome compute_lidar_calibration(
    const CalibrationTargetData& target_data,
    const PointCloud& raw_measurements,
    jewels::FactoryOut<SensorCalibration> calibration_out)
{
  if (!validate_target_data(target_data) || raw_measurements.empty())
  {
    return failure;
  }

  // Perform expensive calibration computation
  auto rotation = compute_rotation_matrix(target_data, raw_measurements);
  auto translation = compute_translation_vector(target_data, raw_measurements);
  auto scale = compute_scale_factor(target_data, raw_measurements);

  // Construct the calibration object in-place only when we know we can succeed
  calibration_out->emplace(rotation, translation, scale);
  return success;
}

// Call site
jewels::FactoryResult<SensorCalibration> calibration;
if (fails(compute_lidar_calibration(target_data, point_cloud, jewels::Out{calibration})))
{
  return failure;
}

// Because the function has a documented post-condition, we do not need to check if the calibration is valid here
// because we've already checked the function's outcome.
const auto calibrated_data = calibration->apply_calibration(sensor_data);

// We could optionally check the outcome defensively, like this:
if (!calibration.has_value())
{
  return failure;
}

// Or better yet, at the call site:
if (fails(compute_lidar_calibration(target_data, point_cloud, jewels::Out{calibration})) || !calibration.has_value())
{
  return failure;
}
const auto calibrated_data = calibration->apply_calibration(sensor_data);
```

## Output Parameter Conventions

### Parameter Ordering

When using individual parameters (not parameter structs):

- Output parameters come **before** input parameters
- Exception: `OptionalOut` parameters may come after input parameters and may have default values of `std::nullopt`

```cpp
// Standard ordering
jewels::BinaryOutcome func(jewels::Out<Result> result_out, const Input& input);

// OptionalOut can come last with default
jewels::BinaryOutcome func(const Input& input, jewels::OptionalOut<Debug> debug_out = std::nullopt);
```

### Parameter Structs

When there are many parameters, use separate structs for outputs and inputs:

- Output parameter struct comes first
- Input parameter struct comes second
- If there's only one output parameter, it can be passed directly even if input parameters are in a struct
- Vice versa, if there's only one input parameter, it can be passed directly even if output parameters are in a struct

```cpp
struct OutParams {
  Out<Result> result;
  MaybeOut<Extra> extra_maybe;
  OptionalOut<Debug> debug_opt{std::nullopt};
};

struct InParams {
  const Config& config;
  int threshold;
};

jewels::BinaryOutcome complex_function(OutParams out_params, const InParams& in_params);

// Call site with designated initializers
Result result;
Data data;
Debug debug;
Extra extra;
jewels::MaybeOut maybe_extra{extra};

if (jewels::fails(complex_function(
    {.result = jewels::Out{result},
     .data = data,
     .debug_opt = jewels::OptionalOut{debug},
     .extra_maybe = maybe_extra},
    {.config = my_config, .threshold = 100}))) {
  return jewels::failure;
}
```

### Naming Conventions

- Output parameters do not require special naming suffixes (the type makes intent clear)
- Optional suffixes like `_out`, `_opt_out` may be used if desired

### Usage Guidance

Always use one of these types for output parameters rather than mutable references or pointers.

#### Use `Out<T>` when

- The output is always required and always produced (on success)
- Function failure is indicated by the return value

#### Use `OptionalOut<T>` when

- The caller sometimes needs the output and sometimes doesn't
- The output is large or computing the output is expensive and should be skipped when not needed
- Adding optional outputs to existing functions without breaking call sites

#### Use `MaybeOut<T>` when

- The function can succeed at its primary task but may not always produce this specific output
- You have multiple outputs and some may be unavailable even on overall success
- The output depends on runtime conditions that don't constitute function failure

#### Use `FactoryOut<T>` when

- The output type is not default constructible, or default construction is expensive and should be avoided
- The function always constructs the output when the function succeeds (otherwise use `Out<std::optional<T>>`)

**Avoid `MaybeOut<T>`** if the inability to produce the output should be considered function failure.
Use your return value (`Outcome`) to signal such failures instead.

**Prefer `Out<T>` over `FactoryOut<T>`** when the type is default constructible and construction is not expensive.
