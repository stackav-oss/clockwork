# Signal Extractor

The Signal Extractor provides a Python API for accessing observability signal metadata and extracting report group data from Clockwork log files.

## Features

- Read signal description metadata from log files
- Query available cog class paths, instance paths, report groups, and signals
- Stream report group data lazily with per-channel reading
- Bulk extract report group data with flexible filtering

## Getting Started

Create a `SignalExtractor` with the path or URI to a Clockwork log file:

```python
from clockwork.tools.signal_extractor.signal_extractor import (
    SignalExtractor,
    SignalExtractionFilter,
)

extractor = SignalExtractor("s3://logs/truck_123/run.clog")
```

## Exploring Metadata

The extractor provides methods to discover what data is available in a log.

```python
# List all cog class paths
cog_paths = extractor.list_cog_paths()

# List cog instance paths, optionally filtered by cog class path
all_instances = extractor.list_cog_instance_paths()
class_instances = extractor.list_cog_instance_paths(cog_path="perception::PerceptionCog")

# List report groups, optionally filtered by cog instance path
all_report_groups = extractor.list_report_groups()
instance_rgs = extractor.list_report_groups(cog_instance_path="perception.instance.fqn")

# List signals, optionally filtered by report group
all_signals = extractor.list_signals()
rg_signals = extractor.list_signals(report_group_name="health_metrics")

# List all signal instance names
instance_names = extractor.list_signal_instance_names()
```

## Filtering

`SignalExtractionFilter` controls which data is extracted.
All fields are optional and default to `None` (no filtering).
When multiple fields are set, they combine with AND logic.
Each list field uses OR logic within itself.

```python
from clockwork.tools.signal_extractor.signal_extractor import SignalExtractionFilter

# Filter by cog class path and time range
f = SignalExtractionFilter(
    cog_paths=["perception::PerceptionCog"],
    start_time_ns=1_000_000_000,
    end_time_ns=2_000_000_000,
)

# Filter by specific report groups and signals
f = SignalExtractionFilter(
    report_group_names=["health_metrics"],
    signal_ids=["temperature"],
)

# Filter by specific cog instance paths
f = SignalExtractionFilter(
    cog_instance_paths=["perception.instance.fqn", "localization.instance.fqn"],
)

# Filter by signal instance names
f = SignalExtractionFilter(
    signal_instance_ids=["temp_left", "pressure_main"],
)
```

### Filter Fields

| Field                 | Type                | Description                            |
| :-------------------- | :------------------ | :------------------------------------- |
| `cog_paths`           | `list[str] \| None` | Cog class paths to include             |
| `cog_instance_paths`  | `list[str] \| None` | Cog instance paths to include          |
| `report_group_names`  | `list[str] \| None` | Report group names to include          |
| `signal_ids`          | `list[str] \| None` | Signal names to include                |
| `signal_instance_ids` | `list[str] \| None` | Signal instance names to include       |
| `start_time_ns`       | `int \| None`       | Start of time range (unix nanoseconds) |
| `end_time_ns`         | `int \| None`       | End of time range (unix nanoseconds)   |

> [!NOTE]
> When both `signal_ids` and `signal_instance_ids` are specified, the result is the intersection (AND) of the two filters.

## Streaming Extraction

The `stream()` method returns an iterator of `StreamingReportGroup` objects.
Data is lazily read from the log file only when `batches()` is called.
This is ideal for memory-efficient processing, interactive exploration, or early termination.

```python
for report_group in extractor.stream(
    SignalExtractionFilter(cog_paths=["perception::PerceptionCog"])
):
    print(f"Report group: {report_group.report_group_name}")
    print(f"Cog instance: {report_group.cog_instance_path}")
    print(f"Type: {report_group.report_group_type}")

    for batch in report_group.batches():
        print(f"  Publish time: {batch.publish_time_ns}")
```

Each `StreamingReportGroup` exposes metadata fields:

- `report_group_name` — name of the report group
- `cog_path` — fully-qualified cog class path
- `cog_instance_path` — fully-qualified cog instance path
- `channel_name` — log channel name
- `log_type` — telemetry log type
- `report_group_type` — `"Batched"` or `"Aggregated"`
- `aggregation_size` — number of executions aggregated
- `min_duration_ns` / `max_duration_ns` — duration bounds
- `signal_info` — list of `ReportGroupSignalInfo` for signals in this group

Calling `batches()` yields either `ReportGroupBatch` or `ReportGroupAggregation` objects depending on the report group type.
The `batches()` method can be called multiple times; each call creates a fresh iterator.

## Bulk Extraction

The `bulk_extract()` method reads all matching data at once into a `BulkSignalData` container.
This is suited for automated pipelines or comprehensive analysis where all data is needed.

```python
data = extractor.bulk_extract(
    SignalExtractionFilter(
        cog_paths=["perception::PerceptionCog"],
        start_time_ns=1_000_000_000,
        end_time_ns=5_000_000_000,
    )
)

# Iterate over all extracted report groups
for rg in data.report_groups:
    print(f"{rg.cog_instance_path}/{rg.report_group_name}: {len(rg.data)} messages")

# Look up by cog class path
for rg in data.by_cog_path.get("perception::PerceptionCog", []):
    process(rg)

# Look up by cog instance path
for rg in data.by_cog_instance_path.get("perception.instance.fqn", []):
    process(rg)
```

`BulkSignalData` provides:

- `report_groups` — flat list of `ExtractedReportGroup` objects
- `by_cog_path` — dict mapping cog class path to its report groups
- `by_cog_instance_path` — dict mapping cog instance path to its report groups

## Data Structures

### `ReportGroupBatch`

Represents a single batched report group message.
Each entry in `entries` corresponds to one cog execution within the batch.

```python
@dataclass
class ReportGroupBatch:
    publish_time_ns: int
    execution_count: int
    execution_interval_ns: int
    entries: list[dict[str, object | None]]
```

Field names in each entry follow the `{signal}_{pre_agg}` naming convention.
Every generated field remains in the entry when its signal is missing, but its value is `None`.
This includes preserved signal metadata fields.
The generated `signal_presence` field is consumed by the extractor and is not returned.

### `ReportGroupAggregation`

Represents a single post-aggregated report group message.

```python
@dataclass
class ReportGroupAggregation:
    publish_time_ns: int
    execution_count: int
    execution_interval_ns: int
    fields: dict[str, object | None]
```

Field names follow the `{signal}_{pre_agg}_{post_agg}` naming convention defined in PDD-933.
Every generated field for a missing signal has value `None`.
Logs whose metadata predates explicit validity tracking retain the previous assume-present behavior.

### `ReportGroupSignalInfo`

Metadata about a signal within a report group.

```python
@dataclass
class ReportGroupSignalInfo:
    signal_name: str
    signal_index: int
    alias: str | None
    pre_aggregation_types: list[str]
    post_aggregation_types: list[str]
```

### `ExtractedReportGroup`

A fully extracted report group returned by `bulk_extract()`.
Contains both metadata and the list of extracted messages.

```python
@dataclass
class ExtractedReportGroup:
    report_group_name: str
    cog_path: str
    cog_instance_path: str
    channel_name: str
    log_type: str
    report_group_type: str
    aggregation_size: int
    min_duration_ns: int
    max_duration_ns: int
    signal_info: list[ReportGroupSignalInfo]
    data: list[ReportGroupAggregation | ReportGroupBatch]
```

## End-to-End Example

```python
from clockwork.tools.signal_extractor.signal_extractor import (
    SignalExtractor,
    SignalExtractionFilter,
)

extractor = SignalExtractor("s3://logs/truck_123/run.clog")

# Discover what is in the log
print("Cog paths:", extractor.list_cog_paths())
print("Signals:", extractor.list_signals())

# Stream a specific report group and print batch data
for rg in extractor.stream(
    SignalExtractionFilter(report_group_names=["health_metrics"])
):
    print(f"\n--- {rg.cog_instance_path} / {rg.report_group_name} ---")
    for sig in rg.signal_info:
        print(f"  Signal: {sig.signal_name} (index={sig.signal_index})")

    for batch in rg.batches():
        print(f"  Time: {batch.publish_time_ns}, entries: {batch.execution_count}")

# Bulk extract for offline analysis
data = extractor.bulk_extract(
    SignalExtractionFilter(signal_ids=["temperature"])
)
for rg in data.report_groups:
    for msg in rg.data:
        print(msg)
```
