# Clockwork Observability Signals

This guide covers how to define, configure, and use Clockwork Observability Signals in your cogs.
Signals provide a standardized mechanism for producing scalar measurements that are automatically aggregated, logged, and made available for offline analysis and dashboards.

## Key Concepts

**Signal**: A named scalar measurement produced by a cog.
Valid signal types include integers, floats, booleans, enums, `Duration`, `SyncTime`, and strong types with scalar underlying types.
Schemas, arrays, optionals, and other complex types are not valid signal types.

**Signal instance**: A specific occurrence of a signal, distinguished by an instance name.
For example, a `CameraModelRuntime` signal might have instances for each camera: `CameraModelRuntime[forward_right]`, `CameraModelRuntime[forward_left]`.
Signal instances are static at system composition time; they cannot be created dynamically at runtime.

**Report group**: A named collection of signals within a cog that share a reporting strategy (batched or post-aggregated) and logging policy.
Each report group maps to a generated Clockwork schema and is published on its own channel.

**Pre-aggregation**: Aggregation applied immediately when a signal value is produced, within a single cog execution.
Use this when you measure the same thing multiple times per execution (e.g., iterating over a collection).

**Post-aggregation**: Aggregation applied across multiple cog executions before the report is published.
Use this to reduce logging volume by summarizing values over a reporting window.

**Reporting strategy**: Report groups use either **batched** (retain individual per-execution values up to a batch size) or **post-aggregated** (aggregate values across executions before publishing) strategies.

## Defining Signals in the DSL

### Module-Scope Signals

Signals can be defined at module scope so that multiple cogs can reference the same signal definition.
Module-scope signals can only specify pre-aggregation and multi-instance options; post-aggregation is only allowed when a signal is assigned to a report group inside a cog.

```clk
// Duration of the camera model execution
signal CameraModelRuntime: Duration
{
    pre_aggregation: ["min", "max"];
    multi_instance: true;
}
```

### Cog-Scope Signals and Report Groups

Signals are most commonly defined inside a cog's `signals` block, which assigns them to a named report group.
A cog can have multiple report groups.

```clk
use std::signals::{ReportGroupPolicy, ReportingStrategy, ReportGroupLogType};

// My processing cog
cog MyProcessorCog
{
    signals my_metrics
    {
        // Latency of each processing cycle
        process_latency: signal Duration
        {
            post_aggregation: ["min", "max", "mean"];
        }

        // Number of items processed per cycle
        items_processed: signal UInt32
        {
            pre_aggregation: ["sum"];
            post_aggregation: ["max", "mean"];
        }

        // Whether processing succeeded
        processing_ok: signal Bool;
    }

    signals detailed_stats
    {
        // Per-item processing time (accumulated multiple times per execution)
        per_item_time: signal Duration
        {
            pre_aggregation: ["min", "max", "mean"];
        }
    }

    inputs
    {
        work_items: Tappy<WorkItemMessage>;
    }

    execution
    {
        condition new_work: new_message(work_items);
        execute when: new_work;
    }
}
```

### Signal Options Reference

Each signal definition supports the following options inside its block:

- **`pre_aggregation`**: List of aggregation types applied within a single cog execution.
  Default is `"value"` (no aggregation, the signal is set once per execution).
  Valid values: `"value"`, `"min"`, `"max"`, `"sum"`, `"count"`, `"mean"`, `"final_value"`, `"first_value"`.

- **`post_aggregation`**: List of aggregation types applied across cog executions within a reporting window.
  Only allowed on cog-scope signals (inside a `signals` block on a cog), not on module-scope signals.
  Valid values: `"min"`, `"max"`, `"sum"`, `"count"`, `"mean"`, `"final_value"`, `"first_value"`.

- **`multi_instance`**: Boolean indicating whether the signal supports multiple instances (one per cog instance).
  Default is `false`.

- **`metadata`**: A type associated with each signal value.
  Metadata is preserved through `min`, `max`, `first_value`, `final_value`, and `value` aggregations.
  Metadata is stripped by `sum`, `count`, and `mean` aggregations.

### Referencing Module-Scope Signals in Report Groups

A cog can include a module-scope signal in a report group and add post-aggregation at that point:

```clk
// Duration of the camera model execution
signal CameraModelRuntime: Duration
{
    multi_instance: true;
}

// Camera perception cog
cog CameraPerceptionCog
{
    signals perf_metrics
    {
        // Module signal referenced with post-aggregation added
        CameraModelRuntime
        {
            post_aggregation: ["min", "max"];
        }
    }

    execution
    {
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
}
```

### Multiple Instances of the Same Signal

A single report group can include multiple instances of the same signal, each with a different identifier and instance name.
This is useful when a cog processes data from multiple sources and needs to track metrics separately for each source.
However, note that if an instance name is explicitly defined in a report group, it effectively prevents the cog from being instantiated more than once.
If the cog is instantiated multiple times, there will be multiple instantiations of the signal instance that have same signal name and instance, which is an error.

#### Example: Signal Defined in Shared Module

First, define the signal in a shared module file (e.g., `common_signals.clk`):

```clk
// common_signals.clk
// Shared signal for tracking processing latency
signal ProcessingLatency: Duration
{
    multi_instance: true;
}
```

#### Example: Different Cogs Using the Same Signal

Multiple cogs can reference the same module-scope signal:

```clk
// sensor_processor.clk
use myproject::common_signals::ProcessingLatency;

// Processes data from sensor A
cog SensorAProcessor
{
    signals metrics
    {
        ProcessingLatency
        {
            post_aggregation: ["min", "max", "mean"];
        }
    }

    execution
    {
        condition periodic: time_since_last_exec(50ms);
        execute when: periodic;
    }
}

// Processes data from sensor B
cog SensorBProcessor
{
    signals metrics
    {
        ProcessingLatency
        {
            post_aggregation: ["min", "max", "mean"];
        }
    }

    execution
    {
        condition periodic: time_since_last_exec(50ms);
        execute when: periodic;
    }
}
```

When instantiated in the system, each cog instance will produce its own `ProcessingLatency` signal instance, automatically distinguished by the cog instance name (e.g., `ProcessingLatency[sensor_a_processor]`, `ProcessingLatency[sensor_b_processor]`).

#### Example: Single Cog with Multiple Signal Instances

A single cog can also include multiple instances of the same signal within one report group.
This requires assigning different identifiers and instance names to each:

```clk
// fusion_processor.clk
use myproject::common_signals::ProcessingLatency;

// Processes data from multiple input sources
cog FusionProcessor
{
    signals metrics
    {
        // First instance for camera processing
        camera_latency: ProcessingLatency
        {
            instance_name: "camera_processing";
            post_aggregation: ["min", "max"];
        }

        // Second instance for lidar processing
        lidar_latency: ProcessingLatency
        {
            instance_name: "lidar_processing";
            post_aggregation: ["min", "max"];
        }

        // Third instance for radar processing
        radar_latency: ProcessingLatency
        {
            instance_name: "radar_processing";
            post_aggregation: ["min", "max"];
        }
    }

    execution
    {
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
}
```

In the generated C++ API, these become distinct methods:

```cpp
void fusion_processor_function(FusionProcessorDial& dial) {
  auto& signals = dial.get_signals();

  // Measure camera processing
  const auto camera_start = jewels::time::SyncTime::now();
  process_camera_data();
  signals.set_camera_latency(jewels::time::SyncTime::now() - camera_start);

  // Measure lidar processing
  const auto lidar_start = jewels::time::SyncTime::now();
  process_lidar_data();
  signals.set_lidar_latency(jewels::time::SyncTime::now() - lidar_start);

  // Measure radar processing
  const auto radar_start = jewels::time::SyncTime::now();
  process_radar_data();
  signals.set_radar_latency(jewels::time::SyncTime::now() - radar_start);
}
```

Each signal instance will have its own set of methods:

- `set_camera_latency()` / `get_camera_latency_value_min()` / `get_camera_latency_value_max()`
- `set_lidar_latency()` / `get_lidar_latency_value_min()` / `get_lidar_latency_value_max()`
- `set_radar_latency()` / `get_radar_latency_value_min()` / `get_radar_latency_value_max()`

The `instance_name` option controls the runtime instance name used in logs and dashboards, while the identifier (`camera_latency`, `lidar_latency`, `radar_latency`) is used for the C++ API method names and schema field names.

## Report Group Policies

Every report group must have a `ReportGroupPolicy` applied to it.
The policy controls the reporting strategy, logging behavior, and reporting window.

```clk
use std::signals::{ReportGroupPolicy, ReportingStrategy, ReportGroupLogType};

policy ReportGroupPolicy for MyProcessorCog.my_metrics
{
    reporting_strategy = ReportingStrategy::post_aggregated;
    log_type = ReportGroupLogType::event;
    max_observations = 10;
}

policy ReportGroupPolicy for MyProcessorCog.detailed_stats
{
    reporting_strategy = ReportingStrategy::batched;
    log_type = ReportGroupLogType::non_redundant_telemetry;
    max_observations = 25;
    min_duration = 100ms;
    max_duration = 1s;
}
```

### Policy Fields

- **`reporting_strategy`**: Either `ReportingStrategy::batched` or `ReportingStrategy::post_aggregated`.
- **`log_type`**: Either `ReportGroupLogType::event`, `ReportGroupLogType::non_redundant_telemetry`, or `ReportGroupLogType::none`.
- **`min_observations`**: Minimum number of cog executions before the report can be published.
- **`max_observations`**: Maximum number of executions in a reporting window.
  For batched groups, this determines the batch size.
  For post-aggregated groups, reaching this count (if `min_duration` is also met) triggers publishing.
- **`min_duration`**: Minimum elapsed time before the report can be published.
- **`max_duration`**: Maximum elapsed time before the report is published regardless of observation count.

### Choosing Batched vs Post-Aggregated

Use **batched** when you need to retain the individual per-execution signal values.
The batch stores one entry per cog execution up to `max_observations`, then publishes the full batch.
This is useful when you want to analyze distributions or individual data points offline.

Use **post-aggregated** when you want to summarize signals across executions to reduce data volume.
Post-aggregation computes aggregates (min, max, mean, etc.) over the reporting window.
This is useful for metrics where the summary statistics are sufficient.

## Generated Tachyon Schemas

The Clockwork compiler automatically generates tachyon schemas for each report group.
You do not write these schemas by hand; they are derived from your signal definitions and the report group policy.

### Post-Aggregated Report Group Schema

For a post-aggregated report group, the generated schema has one field per combination of `(signal, pre_aggregation, post_aggregation)`.
Schema name: `{CogName}{ReportGroupName}`

Given:

```clk
// A processing cog
cog Foo
{
    signals metrics
    {
        // Tracking a value with default pre-aggregation and min/max post-aggregation
        tracked_value: signal Int64
        {
            post_aggregation: ["min", "max"];
        }

        // Accumulating a value with sum pre-aggregation and mean/max post-aggregation
        accumulated: signal UInt32
        {
            pre_aggregation: ["sum"];
            post_aggregation: ["mean", "max"];
        }
    }
    // ... execution block ...
}
```

The generated schema is conceptually:

```clockwork
// Post-aggregated metrics for Foo
schema FooMetrics
{
    fields
    {
        // Number of cog executions in this reporting window
        #0 execution_count: UInt16;
        // Total duration of this reporting window
        #1 execution_interval: Duration;
        // Minimum of tracked_value across executions
        #2 tracked_value_value_min: Int64;
        // Maximum of tracked_value across executions
        #3 tracked_value_value_max: Int64;
        // Mean of accumulated sum across executions (always Float64)
        #4 accumulated_sum_mean: Float64;
        // Maximum of accumulated sum across executions
        #5 accumulated_sum_max: UInt32;
    }
}
```

The naming convention for fields is: `{identifier}_{pre_aggregation}_{post_aggregation}`.

If a signal has metadata and both pre and post aggregation preserve metadata, a corresponding `_metadata` field is also generated.

### Batched Report Group Schema

For a batched report group, the generated schema uses a `VarSoa` (Structure of Arrays) container.
Two schemas are generated: one for the per-entry element, and one for the overall message.

Given:

```clk
// A processing cog
cog Bar
{
    signals batch_metrics
    {
        // Simple value signal
        latency: signal Duration;

        // Signal with min/max pre-aggregation
        score: signal Float32
        {
            pre_aggregation: ["min", "max"];
        }
    }
    // ... execution block ...
}
```

The generated schemas are conceptually:

```clockwork
// Per-execution element for batched batch_metrics signals
schema BatchMetricsSignal
{
    options { soa_enabled: true; }
    fields
    {
        // Latency value for this execution
        #0 latency_value: Duration;
        // Minimum score observed during this execution
        #1 score_min: Float32;
        // Maximum score observed during this execution
        #2 score_max: Float32;
    }
}

// Batched report for Bar batch_metrics
schema BarBatchMetrics
{
    fields
    {
        // Total duration of this batch window
        #0 execution_interval: Duration;
        // Array of per-execution signal entries
        #1 signals: VarSoa<BatchMetricsSignal, batch_size>;
    }
}
```

The naming convention for element fields is: `{identifier}_{pre_aggregation}`.

> [!NOTE]
> For batched report groups, `mean` pre-aggregation is expanded to `sum` and `count` fields.
> The mean can then be computed in post-processing as `sum / count`.

### Report Group Channels

Each report group is published on an auto-generated channel with the naming convention:

```clockwork
/_clockwork/report-groups/{CogName}/{group_name}/{uuid}
```

Where `{uuid}` is a unique identifier for the cog instance.
These channels are automatically subscribed to by the logger based on the `log_type` in the policy.

## Using the Generated C++ Signal API

The Signal API is code-generated as a `SignalApi` struct nested inside the Dial.
The API is flat: signals from all report groups are accessible through a single interface.
This means report groups can be refactored without changing your C++ code.

### Accessing the Signal API

From within your cog function, access the signal API through the Dial:

```cpp
void my_cog_function(MyCogDial& dial) {
  auto& signals = dial.get_signals();
  // Use signals.set_* or signals.accumulate_* methods
}
```

### Setting Signal Values

For signals **without** pre-aggregation (default `"value"`), use `set_`:

```cpp
// Signal defined as: tracked_value: signal Int64;
signals.set_tracked_value(42);

// Signal with metadata defined as:
//   my_signal: signal Float32 { metadata: SyncTime; }
signals.set_my_signal(3.14f, current_time);
```

> [!WARNING]
>
> `set_` should be called at most once per cog execution for a given signal.
> Calling it more than once is considered incorrect usage (though it will not error at runtime; the last value wins).

### Accumulating Signal Values

For signals **with** pre-aggregation, use `accumulate_`:

```cpp
// Signal defined as: per_item_time: signal Duration { pre_aggregation: ["min", "max"]; }
for (const auto& item : items) {
  const auto start = now();
  process(item);
  const auto elapsed = now() - start;
  signals.accumulate_per_item_time(elapsed);
}
```

`accumulate_` can be called multiple times per cog execution.
The pre-aggregators track the running aggregate (min, max, sum, count, etc.) across all calls within that execution.

### Retrieving Current Values

Getter methods let you query the current aggregated values.
All getters return `jewels::BinaryOutcome` and use `jewels::Out<T>` output parameters:

```cpp
// Single aggregation: get_<identifier>()
int64_t value = 0;
if (jewels::ok(signals.get_tracked_value(jewels::Out{value}))) {
  // Use value
}
```

For signals with **multiple** pre-aggregations, the getter name includes the aggregation type:

```cpp
// Multiple pre-aggregations: get_<identifier>_<agg>()
int32_t min_val = 0;
int32_t max_val = 0;
if (jewels::ok(signals.get_score_min(jewels::Out{min_val}))) { /* ... */ }
if (jewels::ok(signals.get_score_max(jewels::Out{max_val}))) { /* ... */ }
```

For signals with post-aggregation and multiple aggregation combinations, the getter includes both types:

```cpp
// Post-aggregated: get_<identifier>_<pre_agg>_<post_agg>()
uint64_t min_of_values = 0;
uint64_t max_of_values = 0;
if (jewels::ok(signals.get_tracked_value_value_min(jewels::Out{min_of_values}))) { /* ... */ }
if (jewels::ok(signals.get_tracked_value_value_max(jewels::Out{max_of_values}))) { /* ... */ }
```

When a signal has metadata preserved through aggregation, the getter accepts an optional metadata output:

```cpp
float value = 0.0f;
jewels::time::SyncTime metadata{};
if (jewels::ok(signals.get_my_signal(jewels::Out{value}, jewels::OptionalOut{metadata}))) {
  // value and metadata are populated
}
```

Getters return `jewels::failure` if the signal has not yet been set or accumulated in the current reporting interval.

### Batch Management (Public Methods)

For batched report groups, the following public methods are available:

```cpp
// Get the number of completed executions in the current batch
size_t count = signals.get_batch_count_<group>();

// Get the batch size (max_observations from the policy)
size_t size = signals.get_batch_size_<group>();
```

For post-aggregated report groups:

```cpp
// Get the number of executions since the last reset
size_t exec_count = signals.get_execution_count_<group>();
```

Batch lifecycle management (reset, start/end of execution, should-publish, populate) is handled automatically by the generated Policy class.
Cog code does not call these methods directly.

## Method Naming Summary

The mapping from DSL definition to generated method names:

| DSL Definition                   | Generated Method                        |
| :------------------------------- | :-------------------------------------- |
| No pre-aggregation               | `set_<identifier>(value)`               |
| With pre-aggregation             | `accumulate_<identifier>(value)`        |
| Single aggregation               | `get_<identifier>(Out<T>)`              |
| Multiple pre-aggregations        | `get_<identifier>_<agg>(Out<T>)`        |
| Post-aggregated, single combo    | `get_<identifier>(Out<T>)`              |
| Post-aggregated, multiple combos | `get_<identifier>_<pre>_<post>(Out<T>)` |

Where `<identifier>` is the signal identifier from the DSL, and `<agg>`, `<pre>`, `<post>` are the aggregation type names (e.g., `min`, `max`, `sum`, `count`, `mean`, `value`, `final_value`, `first_value`).

## Aggregation Types Reference

| Type          | Description              | Preserves Metadata | Batched Expansion           |
| :------------ | :----------------------- | :----------------- | :-------------------------- |
| `value`       | No aggregation (default) | Yes                | N/A                         |
| `min`         | Minimum value            | Yes                | -                           |
| `max`         | Maximum value            | Yes                | -                           |
| `sum`         | Sum of all values        | No                 | -                           |
| `count`       | Number of observations   | No                 | -                           |
| `mean`        | Mean of all values       | No                 | Expanded to `sum` + `count` |
| `first_value` | First observed value     | Yes                | -                           |
| `final_value` | Last observed value      | Yes                | -                           |

### Pre and Post Aggregation Combinations

Pre and post aggregation can be combined freely.
For example, `pre_aggregation: ["min"]` with `post_aggregation: ["max", "mean"]` produces:

- `max(min(values per execution))` across the reporting window
- `mean(min(values per execution))` across the reporting window

When using `mean` post-aggregation, the schema field is always `Float64` regardless of the signal's declared type.

## Complete Example

### DSL Definition

```clk
use std::signals::{ReportGroupPolicy, ReportingStrategy, ReportGroupLogType};

// A sensor processing cog
cog SensorProcessor
{
    signals perf_metrics
    {
        // Cycle time measured once per execution
        cycle_time: signal Duration
        {
            post_aggregation: ["min", "max", "mean"];
        }

        // Per-item latency (accumulated multiple times per execution)
        item_latency: signal Duration
        {
            pre_aggregation: ["min", "max"];
            post_aggregation: ["min", "max"];
        }

        // Count of errors per execution
        error_count: signal UInt32
        {
            pre_aggregation: ["sum"];
            post_aggregation: ["sum", "max"];
        }
    }

    inputs
    {
        sensor_data: Tappy<SensorDataMessage>;
    }

    execution
    {
        condition new_data: new_message(sensor_data);
        execute when: new_data;
    }
}

policy ReportGroupPolicy for SensorProcessor.perf_metrics
{
    reporting_strategy = ReportingStrategy::post_aggregated;
    log_type = ReportGroupLogType::event;
    max_observations = 10;
    max_duration = 1s;
}
```

### C++ Cog Implementation

```cpp
#include "path/to/sensor_processor_clk_cc_dial.hh"

void sensor_processor_function(SensorProcessorDial& dial) {
  auto& signals = dial.get_signals();

  const auto cycle_start = jewels::time::SyncTime::now();

  // Process each item
  const auto& view = dial.sensor_data.view();
  for (const auto& item : view) {
    const auto item_start = jewels::time::SyncTime::now();

    if (jewels::fails(process_item(item))) {
      signals.accumulate_error_count(1U);
    }

    const auto item_elapsed = jewels::time::SyncTime::now() - item_start;
    signals.accumulate_item_latency(item_elapsed);
  }

  const auto cycle_elapsed = jewels::time::SyncTime::now() - cycle_start;
  signals.set_cycle_time(cycle_elapsed);
}
```

The framework handles the report group lifecycle automatically: starting and ending execution windows, accumulating pre-aggregated values into post-aggregators, checking publish conditions, populating the generated schema, and publishing the report on the appropriate channel.
