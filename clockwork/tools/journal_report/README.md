# Journal Report

`journal_report` generates a self-contained static HTML report from a serialized `clockwork.journal.JournalFile`.
The report is an offline inspection artifact for execution timing, replay-readiness gaps, anomaly summaries, execution details, channel flow, message sequences, and state snapshot metadata.

## Generate a Report

Run the tool through Bazel from the repository root.

```bash
bazel run @clockwork//clockwork/tools/journal_report:journal_report -- generate \
  --journal /tmp/example.journal.pb \
  --output /tmp/example.journal.html
```

Use `--title` to override the HTML title.
Use `--max-html-bytes` to tune the report-size warning threshold.
Use `--fail-on-size-warning` when CI or scripted validation should fail after writing an oversized report.

The generated HTML embeds checked-in CSS, checked-in JavaScript, and the report model JSON.
It does not load network resources.

## Inspect Message Data Locally

Use `serve` to inspect messages present in the journal's `ChannelMessage` index.

```bash
bazel run @clockwork//clockwork/tools/journal_report:journal_report -- serve \
  --journal /tmp/example.journal.pb \
  --port 8080
```

The server binds to `127.0.0.1` by default and uses `JournalMetadata.log_uri` unless `--log-uri` overrides it.
Port `0` selects an ephemeral port and the selected address is printed to stderr.
The report is available at `/`.
Clicking an indexed sequence number in an execution fetches that single message from the source log, dynamically deserializes its Tachyon schema, and expands the resulting JSON below the sequence number.
Message data is not loaded until it is clicked.

When running inside an SVW, bind to all container interfaces and forward the selected port to the local browser.

```bash
bazel run @clockwork//clockwork/tools/journal_report:journal_report -- serve \
  --journal /tmp/example.journal.pb \
  --host 0.0.0.0 \
  --port 8080
```

Only use the broader bind with a private workspace port forward because the local server does not authenticate requests and can return logged message data.

## Current Data Contract

The MVP consumes the fields already present in `JournalFile`.
Execution timelines and detail panels come from `CogJournal.executions`.
Replay-readiness and anomaly rows come from `JournalMetadata.replay_readiness`.
Channel flow and message sequence fallbacks come from `ChannelSummary`.
Rich message sequence rows come from optional `ChannelMessage` records.
Producer and consumer execution links are derived from execution input and output sequence records.
State snapshot metadata comes from `StateSnapshot`.

Normal payload bytes are not present in the current journal schema.
When optional `ChannelMessage` records are absent, message sequence sections intentionally show channel-summary metadata and an unavailable-detail note.

## Test Fixtures

Small Python fixture builders live in `tests/support/journal_fixtures.py`.
They cover single-cog, group, missing-data, alignment, and message sequence report cases.
Use these helpers for report rendering tests instead of copying large protobuf setup into every test file.

Run all report tests with:

```bash
bazel test @clockwork//clockwork/tools/journal_report/tests/...
```

## Manual Validation

Generate a journal with the file generator.

```bash
bazel run @clockwork//clockwork/tools/journal_file_generator:journal_file_generator -- generate \
  --log-uri <log-uri> \
  --output /tmp/test.journal.pb \
  --summary-json /tmp/test.journal.summary.json \
  --start-time-ns <start> \
  --end-time-ns <end> \
  --cog-instance-path <resolved-cog-instance-path> \
  --system-target <system-target>
```

Generate the static report from that journal.

```bash
bazel run @clockwork//clockwork/tools/journal_report:journal_report -- generate \
  --journal /tmp/test.journal.pb \
  --output /tmp/test.journal.html
```

Open the HTML file and compare the overview counts, replay-readiness gaps, execution sequence numbers, channel summaries, and state snapshot metadata with the generator summary.
The report should remain usable without network access.
