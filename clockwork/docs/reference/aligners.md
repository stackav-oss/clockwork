# Aligners

Message aligners are Clockwork entities that declaratively specify how to select and group messages from multiple input channels based on constraints and optimization objectives.

## Overview

Aligners solve a common problem in robotics and real-time systems: multiple sensors or data streams produce messages independently, and a downstream component needs to process a coherent set of these messages together.
For example, a perception system might need lidar sweeps, camera images, radar detections, a vehicle pose, and a local map — all selected so that the sensor data is temporally close.

An aligner produces an alignment output message each cycle, telling a downstream consumer cog which messages were selected.
The consumer cog then reads those messages and does its work.
The alignment message itself is small and does not include the content of the aligned messages; it only includes sequence numbers.
The consumer independently receives the aligned messages from their channels and uses the alignment message to correlate them.
All of that is internal plumbing handled by the Clockwork infrastructure; users don't have to make their own channel connections or sequence number matching for the aligned channels; they just connect the aligner output to the consumer's aligned input.

## File Setup

Every `.clk` file defining an aligner needs some things in the header:

- `cpp_aligner` in the generate list (not `cpp_cog`)
- Optionally add `cpp_test_cog` to enable [unit testing](#testing).
- `use @clockwork::std::aligners::state;` _if_ the aligner has optional inputs with non-zero timeouts

> [!IMPORTANT]
> The aligner must be defined in a **separate `.clk` file** from any cog that references its generated alignment schema (the `{AlignerName}AlignmentMsg` type).
> The alignment schema is created after module evaluation, so it is not available for name resolution within the same file.
> Define the aligner in its own `.clk` file and import it into the consumer cog's file.

```clk
#![generate(cpp, cpp_aligner, cpp_test_cog)]
#![cpp(namespace=autonomy::perception)]

use my_messages::{SensorMsg, CameraMsg};
use @clockwork::std::aligners::state;
```

## Syntax

```clk
// Documentation required
aligner <Name>
{
    inputs
    {
        // Documentation for each input
        <input_name>: <MessageType>;
        // Or with properties:
        <input_name>: <MessageType>
        {
            <property>: <value>;
        }
    }

    // Body: assumptions, constraints, objectives, let bindings, functions
}
```

### Full Example

```clk
#![generate(cpp, cpp_aligner, cpp_test_cog)]
#![cpp(namespace=autonomy::perception)]

use my_messages::{LidarSweep, CameraImage, DeltaPose};
use @clockwork::std::aligners::state;

// Aligns lidar and camera data with vehicle pose
aligner SensorFusionAligner
{
    inputs
    {
        // Primary lidar sweep
        lidar: Tappy<LidarSweep>
        {
            max_msgs: 10;
        }
        // Front camera image
        camera: Tappy<CameraImage>
        {
            max_msgs: 10;
        }
        // Vehicle pose for motion compensation (low-rate, reused)
        vehicle_pose: Tappy<DeltaPose>
        {
            max_msgs: 10;
            reuse: true;
        }
    }

    assume(is_strictly_increasing(lidar.observation_time));
    assume(is_strictly_increasing(camera.observation_time));
    assume(is_strictly_increasing(vehicle_pose.time_of_validity));

    // require(...) creates hard constraints
    // Hard: lidar and camera within 100ms
    require(|lidar.observation_time - camera.observation_time| <= 100ms);

    // Hard: pose must be at least as recent as lidar
    require(vehicle_pose.time_of_validity >= lidar.observation_time);

    // minimize(...) and maximize(...) create soft optimization objectives
    // Soft: prefer lidar and camera as close as possible
    minimize(|lidar.observation_time - camera.observation_time|);

    // Soft: prefer most recent lidar
    maximize(lidar.observation_time);
}
```

## Input Properties

Aligner inputs support all the same parameters as cog inputs (e.g. `max_size` as shown above), plus additional aligner-specific parameters.

### Aligner-Specific Properties

| Property              | Type        | Default  | Description                                                                                                                                               |
| --------------------- | ----------- | -------- | --------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `optional`            | `Bool`      | `false`  | Input is not required for an alignment to be produced.                                                                                                    |
| `timeout`             | `Duration`  | —        | How long to wait for this optional input. Required when `optional: true`. No default — omitting it on an optional input is a compile error.               |
| `reuse`               | `Bool`      | `false`  | Allow the same message to be selected in consecutive alignment cycles.                                                                                    |
| `batch_size`          | `[Int,Int]` | `[1, 1]` | `[min, max]` number of messages. `min` must be >= 1 and `min` <= `max`.                                                                                   |
| `arbitrary_selection` | `Bool`      | `false`  | Opt into arbitrary solver selection for this input (suppresses the objective-coverage validation). See [Arbitrary Selection](#arbitrary-selection) below. |

### Cog Input Properties

All standard cog input view parameters are supported except `manual_cursor` (always `true` for aligner inputs):

| Property           | Type   | Default  | Description                                                      |
| ------------------ | ------ | -------- | ---------------------------------------------------------------- |
| `max_msgs`         | `Uint` | Required | Maximum number of messages in the input view.                    |
| `skip_threshold`   | `Uint` | —        | Number of messages before skipping is allowed.                   |
| `safety_margin`    | `Uint` | —        | Safety margin for message consumption.                           |
| `copy_inputs`      | `Bool` | `false`  | Copy input messages rather than using zero-copy views.           |
| `connect_optional` | `Bool` | `false`  | The connection is optional (cog can run without it being wired). |

> [!NOTE]
> The aligner-specific `optional` parameter and the cog `connect_optional` parameter have different semantics.
> `optional` controls whether the aligner can produce alignments without this input.
> `connect_optional` controls whether the input connection is required when wiring the system.

### Optional Inputs

Optional inputs allow partial alignments when the input is not available within the timeout period.
Timeout timers begin when all required inputs are present, so this is the _additional time_ beyond the earliest feasible alignment to wait before giving up on optional inputs.
The timeout can be zero (default) to indicate that the aligner should produce an alignment immediately when all required conditions are met with whatever is currently available on the optional input.
The timeout can be different for each optional input.

Constraints and objectives that reference absent optional inputs are **automatically dropped** at runtime when the optional is not available.
You do not need to wrap every constraint or objective on an optional input in `if has_candidates(...)` — only use conditional specs when you need different behavior depending on presence.

```clk
// Aligner with radar as optional
aligner OptionalRadarAligner
{
    inputs
    {
        // Required lidar input
        lidar: Tappy<LidarSweep>
        {
            max_msgs: 10;
        }
        // Optional radar with 50ms timeout
        radar: Tappy<RadarDetection>
        {
            max_msgs: 10;
            optional: true;
            timeout: 50ms;
        }
    }

    assume(is_strictly_increasing(lidar.observation_time));
    assume(is_strictly_increasing(radar.observation_time));

    // This constraint is automatically dropped when radar is absent.
    // No need for `if has_candidates(radar)` wrapper.
    require(|lidar.observation_time - radar.observation_time| <= 50ms);
}
```

### Reuse Inputs

Reuse allows the same message to participate in multiple consecutive alignment cycles.
This is essential for low-rate inputs consumed by higher-rate alignments:

```clk
// Aligner where map is reused across cycles
aligner ReuseAligner
{
    inputs
    {
        // High-rate sensor data
        lidar: Tappy<LidarSweep>
        {
            max_msgs: 10;
        }
        // Low-rate map (1 Hz) reused across 10 Hz alignments
        local_map: Tappy<Map>
        {
            max_msgs: 5;
            reuse: true;
        }
    }
}
```

Alignment consumers will be able to distinguish new vs reused messages using the normal `new_msg` APIs on the input view.

### Batch Inputs

Batch inputs include multiple consecutive messages (a sub-sequence) in each alignment.

```clk
// Aligner with batch lidar input
aligner BatchAligner
{
    inputs
    {
        // Select 2-5 consecutive lidar sweeps per alignment
        lidar: Tappy<LidarSweep>
        {
            max_msgs: 20;
            batch_size: [2, 5];
        }
        // Reference timestamp input
        tick: Tappy<TickMsg>
        {
            max_msgs: 10;
        }
    }

    assume(is_strictly_increasing(lidar.observation_time));
    assume(is_strictly_increasing(tick.observation_time));

    // For batch inputs, use min() or max() to access field values.
    // min(lidar.observation_time) = first message in batch
    // max(lidar.observation_time) = last message in batch
    require(|tick.observation_time - min(lidar.observation_time)| <= 300ms);
}
```

> [!IMPORTANT]
> In constraints and objectives, you cannot write `batch_input.field` directly.
> Use `min(batch_input.field)` for the first message or `max(batch_input.field)` for the last message.

## Body Statements

The aligner body (after the `inputs` block) contains a sequence of statements that define the alignment specification.
Three kinds of statements are supported:

- **Let bindings**: `let x = <expr>;` — define named values for use in later expressions.
- **Function definitions**: `fn name(params) { <expr> }` — define reusable expressions (template-style, expanded at call sites).
- **Spec statements**: expressions that evaluate to the `Spec` type (constraints, objectives, and assumptions).

### Field Assumptions

Field assumptions declare properties about fields on aligner inputs.
The compiler uses these to validate and classify constraints.

Use `assume(is_strictly_increasing(input.field))` to declare that a field's values strictly increase (without repeats) across messages in the view:

```clk
assume(is_strictly_increasing(lidar.observation_time));
```

Use `assume(is_non_decreasing(input.field))` to declare that a field's values are sorted but may contain consecutive duplicates:

```clk
assume(is_non_decreasing(lidar.observation_time));
```

Use `assume(is_unique(input.field))` to declare that each value appears at most once, without any ordering guarantee:

```clk
assume(is_unique(radar.tag_id));
```

| Property                 | Meaning                                                  | Allowed constraints   |
| ------------------------ | -------------------------------------------------------- | --------------------- |
| `is_strictly_increasing` | Values strictly increase across view messages.           | Ordering and equality |
| `is_non_decreasing`      | Values are sorted; consecutive duplicates are permitted. | Ordering and equality |
| `is_unique`              | Each value appears at most once. No ordering guarantee.  | Equality only         |
| _(none)_                 | No assumptions. Duplicates possible, unordered.          | Equality only         |

`is_strictly_increasing` and `is_unique` are mutually exclusive as the former already implies the latter.
Declaring both on the same field is an error.
`is_non_decreasing` together with `is_unique` is equivalent to `is_strictly_increasing` and is canonicalized as such.
`is_non_decreasing` and `is_strictly_increasing` on the same field is redundant and produces an error.
Declaring the same property twice on the same field is also an error.

Ordering constraints (such as `require(a <= b)` or `require(|a - b| <= d)`) require all referenced fields to be declared sorted — either `is_strictly_increasing` or `is_non_decreasing`.
If they are not, the compiler produces an error with guidance to add the missing assumption.

### Constraints

Constraints declare hard requirements that every valid alignment must satisfy.
Use `require(condition)` where `condition` is a boolean expression:

```clk
assume(is_strictly_increasing(lidar.observation_time));
assume(is_strictly_increasing(camera.observation_time));
assume(is_strictly_increasing(pose.tov));

// Require lidar and camera to be synchronized
require(lidar.observation_time == camera.observation_time);

// Require lidar to precede pose
require(lidar.observation_time <= pose.tov);

// Require lidar and camera to be within 100ms
require(|lidar.observation_time - camera.observation_time| <= 100ms);
```

The compiler recognizes these constraint forms:

| Form                        | Meaning                                                                       |
| --------------------------- | ----------------------------------------------------------------------------- |
| `require(a == b)`           | Exact equality between fields                                                 |
| `require(a <= b)`           | Ordering: `a` must not exceed `b`                                             |
| `require(a >= b)`           | Ordering: `b` must not exceed `a`                                             |
| `require(\|a - b\| <= d)`   | Bounded difference: `a` and `b` within `d`                                    |
| `require(a <= b + LITERAL)` | Offset ordering: `a` must not exceed `b` by more than `LITERAL` (e.g. `50ms`) |
| `require(a >= b + LITERAL)` | Offset ordering: `a` must not be less than `b` by more than `LITERAL`         |

Fields are not limited to timestamps — any field whose type supports subtraction and comparison through the trait system is valid.

Unrecognized constraint forms produce a compile-time error.

> [!NOTE]
> Note that `require(a == b)` uses different search strategies depending on the field assumptions.
> If both fields are `is_strictly_increasing`, equality compiles to an efficient _O(log N)_ binary search.
> Otherwise, it compiles to an equality filter — correct, but potentially slower.

### Objectives

Objectives declare soft optimization goals.
Use `minimize(expr)` or `maximize(expr)`:

```clk
// Minimize temporal spread — keep sensor times close
minimize(spread(lidar.observation_time, camera.observation_time, pose.tov));

// Maximize freshness of pose data
maximize(pose.tov);
```

#### How Objectives Affect Search Performance

The compiler classifies each input's search strategy based on your constraints and objectives.
Understanding this helps you write efficient aligners:

| What you write                                          | Search strategy | Notes                                                 |
| ------------------------------------------------------- | --------------- | ----------------------------------------------------- |
| `minimize(\|a.field - b.field\|)`                       | NEAREST         | _O(log N)_ starting point, then linear scan outward   |
| `maximize(input.field)`                                 | LAST_IN_RANGE   | Reverse scan from end — picks last valid candidate    |
| `minimize(input.field)`                                 | FIRST_IN_RANGE  | Forward scan from begin — picks first valid candidate |
| No objective referencing input                          | ANY_MATCH       | Forward scan — picks first valid candidate (no cost)  |
| Non-separable objective (references 3+ unsolved inputs) | ENUMERATE       | Exhaustive — slowest, avoid when possible             |

Write constraints and objectives so that each input has a clear search direction.

### Available Builtin Functions

The aligner body has access to the following DFL builtin functions:

| Function            | Description                                                                     |
| ------------------- | ------------------------------------------------------------------------------- |
| `min(a, b, ...)`    | Minimum of values, or first-in-batch for batch fields                           |
| `max(a, b, ...)`    | Maximum of values, or last-in-batch for batch fields                            |
| `abs(x)`            | Absolute value                                                                  |
| `clamp(x, lo, hi)`  | Clamp `x` to `[lo, hi]`                                                         |
| `sum(coll)`         | Sum elements of a collection                                                    |
| `count(coll)`       | Count elements of a collection                                                  |
| `mean(coll)`        | Mean of a collection                                                            |
| `any(coll)`         | True if any element is true                                                     |
| `all(coll)`         | True if all elements are true                                                   |
| `map(fn, coll)`     | Apply function to each element                                                  |
| `filter(fn, coll)`  | Keep elements where function returns true                                       |
| `flatten(coll)`     | Flatten a nested collection                                                     |
| `is_positive(x)`    | True if `x > 0`                                                                 |
| `is_negative(x)`    | True if `x < 0`                                                                 |
| `is_zero(x)`        | True if `x == 0`                                                                |
| `spread(ref, t...)` | `sum(map(fn(t) \|t - ref\|, times))` — temporal spread relative to a reference. |

> [!NOTE]
> The `spread()` variadic function will likely be the most commonly-used builtin function.
> It's the best way (in a `minimize` objective) to express the goal of keeping messages across 2+ inputs close together in time.
> Its first input is privileged as the reference input to which all others are compared.

### Composing Constraints and Objectives

Specs compose with `and` — this is pure concatenation, equivalent to writing them as separate statements:

```clk
require(lidar.observation_time == camera.observation_time)
    and require(|lidar.observation_time - pose.tov| <= 100ms);

// Equivalent to:
require(lidar.observation_time == camera.observation_time);
require(|lidar.observation_time - pose.tov| <= 100ms);
```

This is useful (essential) with `has_candidates`.

### Conditional Specs

Use `if has_candidates(input)` when you need **different** behavior depending on whether an optional input is present — not just to guard a constraint (auto-drop handles that):

```clk
// When radar is present, allow a wider lidar-camera window since radar covers the gap.
// When radar is absent, tighten the lidar-camera window.
if has_candidates(radar)
    then require(|lidar.observation_time - camera.observation_time| <= 200ms)
    else require(|lidar.observation_time - camera.observation_time| <= 50ms);
```

`require(true)` is a no-op — it produces no constraint.
It can be used in either the `else` or `then` to create the equivalent of an empty body when something is required syntactically.
`require(false)` is a compile-time error (trivially unsatisfiable).

You don't have to use `if has_candidates` to guard every reference to an optional input — any constraint or objective that references an absent optional input is automatically dropped (see above).
You only need this if you want something other than dropping the optional-referencing specs; if you actually want to change constraints or objectives on _other_ inputs.

## Connecting to Consumer Cogs

A downstream cog consumes aligner output using the `aligned_inputs` block:

```clk
#![generate(cpp, cpp_cog, cpp_test_cog)]
#![cpp(namespace=autonomy::perception)]

use my_aligner::{SensorFusionAligner};

// Cog that processes aligned sensor data
cog FusionCog
{
    inputs
    {
        // Regular (non-aligned) input
        config_update: Tappy<ConfigMsg>;
    }
    aligned_inputs
    {
        // Everything from the aligner in a combined aligned_input
        aligned_sensors: SensorFusionAligner;
    }
    outputs
    {
        result: Tappy<EchoMsg>;
    }
    execution
    {
        condition new_alignment: new_message(aligned_sensors);
        execute when: new_alignment;
    }
}
```

The `aligned_inputs` block wires the consumer to receive the aligner's alignment output message plus access to the upstream data channels.
The consumer cog's `execute when` condition typically triggers on `new_message(aligned_sensors)`.

### Consumer View Overrides

**You probably don't need to override anything — the default consumer view parameters are designed to work well in most cases.**
But the overrides are here to handle special cases.
Everything here affects the alignment consumer cog, not the aligner itself.

Each entry in `aligned_inputs` subscribes the consumer to _N+1_ endpoints: the alignment message and one view per aligner upstream input.
By default each upstream view is auto-sized from the aligner's view of that input so the consumer has headroom to pick up all messages referenced by an alignment before they are pushed out of the view.

You can override view parameters (most commonly `max_msgs`) on a per-upstream basis.
The outer `aligned_inputs` block now accepts two kinds of children:

- Plain `name: value;` pairs that apply to the **alignment-message view** itself (same as before).
- Nested `<upstream_name> { … }` blocks that override the consumer's view of that specific upstream input.

```clk
cog ConsumerCog
{
    aligned_inputs
    {
        aligned: SomeAligner
        {
            // Params without an upstream name apply to the alignment message itself.
            // So this means: Make the last four alignments available. You usually
            // don't want this; you want the most recent alignment only, which is
            // what the default (1) gives you.
            max_msgs: 4;

            // Per-upstream consumer view overrides.
            // You can override input view parameters for each upstream aligned
            // channel. The names here refer to the aligner's input names.
            // Missing fields inherit from the aligner's view of that upstream.
            // By default `max_msgs` is auto-sized to give ≥20% headroom over the
            // aligner. This is good for most cases, but if your cog is very slow
            // relative to the input messages, you might need to increase it.
            delta_pose
            {
                max_msgs: 300;
            }
        }
    }
    // …
}
```

The consumer view's `max_msgs` is always validated to be at least as large as the aligner's `max_msgs` for the same upstream.
Unknown upstream names (not declared on the referenced aligner) raise at IR-construction time.

## Arbitrary Selection

The solver that picks messages for each input level can only do one of:

- Narrow to an exact candidate via a constraint (e.g., `require(|a - b| <= …)` plus sorted fields) or an equality.
- Pick the best candidate in the feasible window according to a `minimize` / `maximize` objective.
- If neither of the above applies, **pick an arbitrary candidate** in the feasible window.

Arbitrary selection is rarely what you want in practice — though it is deterministic behavior, the behavior is not well-specified.
The compiler therefore requires you to acknowledge it explicitly:

- **Non-batched inputs:** if the join plan for the input is `ANY_MATCH` (nothing pins it) and no objective references the input, the compiler raises a `PipelineError` unless the input has `arbitrary_selection: true`.
- **Batched inputs:** both the `min(...)` (first-in-batch) and `max(...)` (last-in-batch) endpoints must appear in at least one objective, otherwise the solver is free to slide the batch boundary within the feasible window. Setting `arbitrary_selection: true` opts out of the check.

Typical fix: add a `minimize` or `maximize` referencing a timestamp on the input, for example `maximize(input.observation_time);` for "pick the most recent candidate", or `minimize` the difference in timestamps relative to another input for "pick the candidate closest to this other input".

## `max_msgs` on Aligner Inputs

`max_msgs` must be set explicitly on every aligner input.
There is no safe default — a silent `max_msgs: 1` combined with a bursty arrival rate or `batch_size` lets the view fill up before the aligner can produce an alignment.

When `batch_size` is set, `max_msgs` must be at least the maximum batch size so the largest batch fits in the aligner's view.
Consumer cogs have independent views of the aligned inputs, which are auto-sized with additional headroom and may be overridden when more headroom is required.

## Testing

For detailed testing instructions, see [Cog Unit Tests — Alignment Unit Testing](cog_unit_tests.md#alignment-unit-testing).
Aligners and consumer cogs also work normally in Clockwork System Runner tests.

## End-to-End Example

This example shows a complete aligner with optional, batch, and reuse inputs, and a consumer cog wired to it.

### Aligner

```clk
#![generate(cpp, cpp_aligner, cpp_test_cog)]
#![cpp(namespace=autonomy::perception)]

use my_msgs::{SensorMsg};
use @clockwork::std::aligners::state;

// Aligns multiple sensors for fusion
aligner FusionAligner
{
    inputs
    {
        // Tick from upstream scheduler
        tick: Tappy<SensorMsg>
        {
            max_msgs: 10;
        }
        // Camera frames
        camera: Tappy<SensorMsg>
        {
            max_msgs: 10;
        }
        // Lidar sweeps (reusable across cycles)
        lidar: Tappy<SensorMsg>
        {
            max_msgs: 10;
            reuse: true;
        }
        // Radar detections (optional, 200ms timeout)
        radar: Tappy<SensorMsg>
        {
            max_msgs: 10;
            optional: true;
            timeout: 200ms;
        }
        // Pose batch (2-4 consecutive messages)
        pose: Tappy<SensorMsg>
        {
            max_msgs: 20;
            batch_size: [2, 4];
        }
    }

    assume(is_strictly_increasing(tick.observation_time));
    assume(is_strictly_increasing(camera.observation_time));
    assume(is_strictly_increasing(lidar.observation_time));
    assume(is_strictly_increasing(radar.observation_time));
    assume(is_strictly_increasing(pose.observation_time));

    require(|tick.observation_time - camera.observation_time| <= 100ms);
    require(|tick.observation_time - lidar.observation_time| <= 200ms);
    require(|tick.observation_time - radar.observation_time| <= 150ms);
    require(|tick.observation_time - min(pose.observation_time)| <= 300ms);

    minimize(|camera.observation_time - lidar.observation_time|);
    maximize(lidar.observation_time);
}
```

### Consumer

```clk
#![generate(cpp, cpp_cog, cpp_test_cog, cpp_combo_test)]
#![cpp(namespace=autonomy::perception)]

use my_aligner::{FusionAligner};
use my_msgs::{OutputMsg};

// Processes aligned sensor data
cog FusionConsumer
{
    aligned_inputs
    {
        aligned_sensors: FusionAligner;
    }
    outputs
    {
        result: Tappy<OutputMsg>;
    }
    execution
    {
        condition new_alignment: new_message(aligned_sensors);
        execute when: new_alignment;
    }
}
```
