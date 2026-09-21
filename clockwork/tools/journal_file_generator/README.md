# Journal File Generator

The journal file generator creates schema-valid `clockwork.journal.JournalFile` protobufs from Clockwork logs.
Use it when you need a compact journal for a resolved runtime cog in a log.

A useful journal request has two parts:

- A log URI and inclusive sync-time range.
- One or more fully resolved runtime `cog_instance_path` values, or resolved runtime `box_instance_path` values with system context.

Source CLK names are not enough for generation because the same box can be instantiated in several systems or several places in one system.
The normal workflow is to discover the runtime path from system context first, then generate a journal for the chosen path.
Prefer `--system-target` with a `topology_summary` Bazel target whenever one exists.
That form reuses the prebuilt topology summary and is much faster than asking the tool to resolve a local system CLK file directly.
Use `--system-clk-file` only as a fallback when no topology target is available.

## Run the Tool

Run the tool from the workspace root through the Clockwork Bazel target:

```bash
bazel run @clockwork//clockwork/tools/journal_file_generator:journal_file_generator -- <command> <options>
```

The available commands are:

- `find-cogs`, which lists runtime cog paths from topology stored with an offloaded telemetry log.
- `list-instances`, which resolves a declared CLK box or cog into deployed runtime cog paths.
- `generate`, which writes a deterministic journal protobuf for one or more resolved runtime cog paths or resolved runtime box paths.

## Discover Cogs From an Offloaded Log

Offloaded telemetry logs contain `journal_topology.pbtxt` in the timestamp directory, one level above the
readable `telemetry/` stream URI.
List every runtime cog path containing a case-sensitive literal substring:

```bash
bazel run @clockwork//clockwork/tools/journal_file_generator:journal_file_generator -- find-cogs \
  --log-uri s3://bucket/path/to/offloaded/telemetry/timestamp/telemetry/ \
  --name-contains Localizer
```

Local paths and `file:` URIs are also supported.
When the topology file was copied separately, pass `--journal-topology-file /path/to/journal_topology.pbtxt`.
The explicit file takes precedence over discovery from `--log-uri`.
Copy one exact result into `generate --cog-instance-path`; generation never selects a substring match automatically.

## Example Multi-Route Workflow

The examples below use the multi-route topology target and sink box from the Clockwork test support package.
Keep this topology-target form for normal use because it is the fastest way to provide system context.

```bash
EXAMPLE_SYSTEM_TARGET=@clockwork//clockwork/tools/topology/tests:test_system_multi_route_topology_summary
EXAMPLE_SOURCE_CLK=clockwork/tests/support/test_system_multi_route.clk
EXAMPLE_BOX=MultiRouteSinkBox
```

First list the runtime cog instances in the sink box:

```bash
bazel run @clockwork//clockwork/tools/journal_file_generator:journal_file_generator -- list-instances \
  --system-target "${EXAMPLE_SYSTEM_TARGET}" \
  --source-clk-file "${EXAMPLE_SOURCE_CLK}" \
  --box-name "${EXAMPLE_BOX}" \
  --format json
```

The output is a JSON list of concrete runtime candidates.
It will look like this, with one object per resolved cog:

```json
[
  {
    "box_instance_path": "@clockwork::clockwork::tests::support::test_system_multi_route.test_system_multi_route.sink_box_1",
    "cog_instance_path": "@clockwork::clockwork::tests::support::test_system_multi_route.test_system_multi_route.sink_box_1.sink_cog",
    "declared_box": "MultiRouteSinkBox",
    "declared_cog": "sink_cog"
  },
  {
    "box_instance_path": "@clockwork::clockwork::tests::support::test_system_multi_route.test_system_multi_route.sink_box_2",
    "cog_instance_path": "@clockwork::clockwork::tests::support::test_system_multi_route.test_system_multi_route.sink_box_2.sink_cog",
    "declared_box": "MultiRouteSinkBox",
    "declared_cog": "sink_cog"
  }
]
```

Copy the `cog_instance_path` value for the cog you want to journal, or copy the shared `box_instance_path` for a report-oriented box journal.
For a single-cog example, use `sink_box_1.sink_cog`.

To ask discovery for only that nested cog, add `--cog-name`:

```bash
bazel run @clockwork//clockwork/tools/journal_file_generator:journal_file_generator -- list-instances \
  --system-target "${EXAMPLE_SYSTEM_TARGET}" \
  --source-clk-file "${EXAMPLE_SOURCE_CLK}" \
  --box-name "${EXAMPLE_BOX}" \
  --cog-name sink_cog \
  --format table
```

## Generate a Single-Cog Journal

Set the log and time range you want to inspect:

```bash
EXAMPLE_LOG_URI=/path/to/log
START_TIME_NS=1720000000000000000
END_TIME_NS=1720000030000000000
SINK_COG='@clockwork::clockwork::tests::support::test_system_multi_route.test_system_multi_route.sink_box_1.sink_cog'
```

Generate the journal and a replay-readiness summary:

```bash
bazel run @clockwork//clockwork/tools/journal_file_generator:journal_file_generator -- generate \
  --log-uri "${EXAMPLE_LOG_URI}" \
  --output /tmp/multi_route_sink_1.journal.pb \
  --summary-json /tmp/multi_route_sink_1.summary.json \
  --start-time-ns "${START_TIME_NS}" \
  --end-time-ns "${END_TIME_NS}" \
  --cog-instance-path "${SINK_COG}" \
  --system-target "${EXAMPLE_SYSTEM_TARGET}" \
  --snapshot-selection closest \
  --verbose
```

For an offloaded telemetry log, omit `--system-target`; the generator discovers the file one level above the
trailing `telemetry/` directory in `--log-uri`.
To generate from an event log, pass the event stream as `--log-uri` and the corresponding telemetry stream as
`--telemetry-log-uri`:

```bash
bazel run @clockwork//clockwork/tools/journal_file_generator:journal_file_generator -- generate \
  --log-uri s3://bucket/path/to/event/timestamp/event-id/event/ \
  --telemetry-log-uri s3://bucket/path/to/telemetry/timestamp/telemetry/ \
  --output /tmp/event.journal.pb \
  --start-time-ns "${START_TIME_NS}" \
  --end-time-ns "${END_TIME_NS}" \
  --cog-instance-path "${SINK_COG}"
```

If the file was copied separately, pass `--journal-topology-file` explicitly.
The legacy `--system-target` and `--system-clk-file` options remain available for older logs.

Pass `--system-target` whenever you can.
Prefer a `topology_summary` target like `${EXAMPLE_SYSTEM_TARGET}` because it is much faster than resolving the system CLK from source.
System context lets generation map event-metrics input and output members to concrete channel names and discover state snapshot channels.

The command prints its final status to stderr and leaves stdout available for future structured output.
A healthy complete single-cog run reports `Replay readiness: sufficient`:

```text
Journal written: /tmp/multi_route_sink_1.journal.pb
Summary JSON written: /tmp/multi_route_sink_1.summary.json
Replay readiness: sufficient; cogs=1; executions=143; gaps=0; state_snapshot=yes
```

Use the JSON summary when a script needs to inspect the result.
Important fields include `execution_count`, `gap_count`, `gaps`, `has_state_snapshot`, `implemented_readiness_checks_passed`, `missing_inputs`, and `sufficient_for_replay`.

## Generate a Group Journal

Repeat `--cog-instance-path` to include multiple cogs in one report-oriented journal.
For example, include both sink cogs:

```bash
SINK_COG_2='@clockwork::clockwork::tests::support::test_system_multi_route.test_system_multi_route.sink_box_2.sink_cog'

bazel run @clockwork//clockwork/tools/journal_file_generator:journal_file_generator -- generate \
  --log-uri "${EXAMPLE_LOG_URI}" \
  --output /tmp/multi_route_sink_group.journal.pb \
  --summary-json /tmp/multi_route_sink_group.summary.json \
  --start-time-ns "${START_TIME_NS}" \
  --end-time-ns "${END_TIME_NS}" \
  --cog-instance-path "${SINK_COG}" \
  --cog-instance-path "${SINK_COG_2}" \
  --system-target "${EXAMPLE_SYSTEM_TARGET}"
```

Group journals are useful for reports and inspection.
They do not imply direct single-cog replay support, so `ReplayReadiness.sufficient_for_replay` stays false.

## Generate a Box Journal

Pass a resolved `box_instance_path` from `list-instances` to include every cog recursively contained in that runtime box instance.
The generator expands the box path through the supplied system context, stores the requested box path in `JournalScope.box_instance_paths`, and stores the expanded cog paths in `JournalScope.cog_instance_paths`.

```bash
SINK_BOX_PATH='@clockwork::clockwork::tests::support::test_system_multi_route.test_system_multi_route.sink_box_1'

bazel run @clockwork//clockwork/tools/journal_file_generator:journal_file_generator -- generate \
  --log-uri "${EXAMPLE_LOG_URI}" \
  --output /tmp/multi_route_sink_box.journal.pb \
  --summary-json /tmp/multi_route_sink_box.summary.json \
  --start-time-ns "${START_TIME_NS}" \
  --end-time-ns "${END_TIME_NS}" \
  --box-instance-path "${SINK_BOX_PATH}" \
  --system-target "${EXAMPLE_SYSTEM_TARGET}"
```

Box journals are report-oriented.
They do not imply direct full-box replay support, and `ReplayReadiness.sufficient_for_replay` stays false.

## Required Options

`list-instances` requires:

- Exactly one of `--system-clk-file` or `--system-target`.
- `--source-clk-file`, pointing to the CLK file that declares the box.
- `--box-name`, matching the declared box name in that source file.

`generate` requires:

- `--log-uri`, recorded in `JournalMetadata.log_uri`.
- `--output` or `-o`, the local protobuf output path.
- `--start-time-ns`, the inclusive start sync time.
- `--end-time-ns`, the inclusive end sync time.
- At least one `--cog-instance-path`, copied from discovery or another trusted source, or at least one `--box-instance-path` with system context.

## Useful Options

`list-instances` supports:

- `--cog-name`, to narrow discovery to one direct cog instance inside the selected box.
- `--format table`, the default human-readable output.
- `--format json`, the deterministic machine-readable output.

`generate` supports:

- `--summary-json`, to write a deterministic replay-readiness summary.
- `--fail-if-not-replayable`, to exit nonzero after writing outputs when replay readiness is insufficient.
- `--verbose`, to print extraction progress and a compact readiness gap summary.
- `--box-instance-path`, to expand one resolved runtime box instance path into every contained cog.
- `--snapshot-selection before`, to choose the latest snapshot at or before `--start-time-ns`.
- `--snapshot-selection after`, to choose the earliest snapshot at or after `--start-time-ns`.
- `--snapshot-selection closest`, to choose the nearest snapshot to `--start-time-ns`, preferring the earlier snapshot on a tie.
- `--system-target`, to resolve channel maps from a `.clk`, `_clk`, or `topology_summary` Bazel target.
- `--system-clk-file`, to resolve channel maps from a local `.clk` file containing exactly one `system_target`; use this only when no topology target exists.

## What the Journal Contains

Generation can populate:

- Request metadata and the resolved cog scope.
- Execution timing, scheduler latency, requeue count, conditions, and stable execution indexes from cog event metrics.
- Input view sequence metadata and output sequence signals when the event-metrics log preserves those fields.
- Scoped input and output channel summaries.
- A selected state snapshot for each resolved cog when a snapshot channel is available in the log.
- Alignment results for logged aligner output channels consumed by scoped cogs.
- Replay-readiness gaps for missing scope, metrics, required input channels, input metadata, output sequence signals, state snapshots, and uncorrelated alignment results.

The generator reads log topic metadata, signal metadata, cog event metrics, raw scoped channel message metadata, selected raw snapshot payloads, and snapshot policy metadata from the optional system context.
It deserializes only alignment-result messages in addition to metadata, metrics, scoped channel headers, and selected raw snapshot payloads.
It does not deserialize normal message payloads when building channel summaries.

## Current Limitations

Generation accepts only resolved runtime `--cog-instance-path` and `--box-instance-path` values.
Use `list-instances` first when you only know the source CLK file and declared box name.

The journal schema currently has one `CogJournal.state_snapshot` field, so generation selects one state snapshot per cog.

Alignment extraction identifies aligner output channels by logged input schemas whose leaf type name ends in `AlignmentMsg`.
When that schema context is unavailable, missing logged alignment channels are still reported as missing required input channels by channel-summary readiness.

Group journals are report-oriented.
They always keep `ReplayReadiness.sufficient_for_replay = false`.
