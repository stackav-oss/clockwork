# Clockwork State and Config Snapshot and Restore

This guide explains how to snapshot Clockwork cog state and configuration data for later restoration offline, e.g. in simulation, testing, and debugging.

## Overview

The snapshot/restore mechanism allows you to:

- Capture the state of Clockwork cogs periodically during online execution
- Capture configuration data once at startup
- Log snapshots for later use
- Restore state and config from logs or files in offline environments
- Handle long-running state that cannot be reconstructed from message data alone

Note that snapshots can be taken both online and offline, but restoration only works in offline environments using the deterministic runner.

## State Snapshots

### Enabling State Snapshots

To enable state snapshotting for a cog, use the `TakeSnapshots` policy in your DSL:

```clockwork
box MyBox
{
  new my_state: State(representation=MyCogState, memory_resource=memory);
  new my_cog: MyCog;

  connect my_state to my_cog.state;

  // Configure snapshots every 1000ms
  apply TakeSnapshots(channel=MyCogStateChannel, interval=1000ms) to my_cog.state;
}
```

Note that the policy is applied to the cog's state endpoint, not the state instance itself.
This is because the cog instance controls when snapshots are taken.
See below for details of when snapshots are taken.

### Snapshot Configuration Options

The `TakeSnapshots` policy accepts the following parameters:

- **`channel`** (required): The channel where snapshots will be published
- **`interval`**: Time interval between snapshots (e.g., `500ms`, `2s`)
- **`cycles`**: Number of cog execution cycles between snapshots

You must specify at least one of `interval` or `cycles`.
If both are specified, a snapshot is taken when either condition is met first.
Both counters (interval and cycles) reset after each snapshot, so both are interpreted as "at least every X".

#### Examples

```clockwork
// Snapshot every 500 milliseconds
apply TakeSnapshots(channel=StateChannel, interval=500ms) to my_cog.state;

// Snapshot every 10 execution cycles
apply TakeSnapshots(channel=StateChannel, cycles=10) to my_cog.state;

// Snapshot every 1 second OR 20 cycles, whichever comes first
apply TakeSnapshots(channel=StateChannel, interval=1s, cycles=20) to my_cog.state;
```

### Logging Snapshots

Snapshots are not automatically logged. You must explicitly configure logging for snapshot channels:

```clockwork
// Configure persistent logging for snapshot channel
policy ChannelLoggingPolicy for MyCogStateChannel
{
  log_type = LogType::event;
  channel_type = ChannelType::persistent;
}
```

**Important**: Typically use `ChannelType::persistent` for snapshot channels to ensure the most recent snapshot is always available at the beginning of event logs.
This is not done automatically.

### When Snapshots Are Taken

- Snapshots are captured **after** a cog instance executes and mutates the state
- The snapshot timestamp matches the transmission time of any messages produced by that cog execution
- The mutex lock is held during snapshot creation to ensure a coherent state capture
- If a cog doesn't execute, no snapshot is taken

It's possible but not recommended to have multiple cogs that share state each take snapshots of the same state instance.
The timing of the snapshots will be independent with each cog maintaining its own cycle and interval counters.

## State Restoration

### Loading State from Logs

To restore state from a logged snapshot, use the `FirstMessage` data source:

```clockwork
box MyBox
{
  new my_state: State(representation=MyCogState, memory_resource=memory);
  new my_cog: MyCog;

  connect my_state to my_cog.state;

  // Create data source from first message on snapshot channel
  snapshot_state = FirstMessage(channel=MyCogStateChannel);

  // Connect data source directly to state instance
  connect snapshot_state to my_state;
}
```

The `FirstMessage` data source has an optional `allow_default` parameter (default `false`).
See the "Handling Missing Snapshots" section below for details.

Note that unlike for snapshots, the data source is connected to the state instance rather than to a cog endpoint.

### Loading State from Files

To restore state from a file on disk, use `SerializedDataFile`:

```clockwork
new my_state: State(representation=MyCogState, memory_resource=memory);
new my_cog: MyCog;

connect my_state to my_cog.state;

// Load from a textproto file
saved_state = SerializedDataFile(
  representation=Protobuf<MyCogState>,
  path="config/saved_state.textproto",
);

connect saved_state to my_state;
```

### Handling Missing Snapshots

If a snapshot is not available in the log, you can configure fallback behavior:

```clockwork
// Option 1: Allow default construction
snapshot_state = FirstMessage(
  channel=MyCogStateChannel,
  allow_default=True
);
connect snapshot_state to my_state;

// Option 2: Provide a fallback data source
snapshot_state = FirstMessage(channel=MyCogStateChannel);
fallback_state = SerializedDataFile(
  path="config/default_state.textproto",
  representation=Protobuf
);

// Connect fallback to snapshot's fallback member
connect fallback_state to snapshot_state.fallback;
connect snapshot_state to my_state;
```

## Configuration Snapshots

### Snapshotting Configuration Once

For configuration data that doesn't change during runtime, use `SnapshotOnce`:

```clockwork
box MyBox
{
  new my_config: SerializedDataFile(representation=Protobuf<MyConfig>, path="config/default_config.textproto");
  new my_cog: MyCog;

  connect my_config to my_cog.config;

  // Snapshot config once at startup
  apply SnapshotOnce(channel=MyConfigChannel) to my_cog.config;
}

policy ChannelLoggingPolicy for MyConfigChannel
{
  log_type = LogType::event;
  channel_type = ChannelType::persistent;
}
```

Just as with state, the snapshot is configured on a cog endpoint, not on the config instance, and logging must be separately configured.
The config data will be published once during initialization before any cog execution.
With a persistent logging policy as shown above, this single snapshot will still be available at the start of every event.

### Restoring Configuration

Configuration restoration uses the same mechanisms as state:

```clockwork
new my_config: FirstMessage(channel=MyConfigChannel);
new config_fallback: SerializedDataFile(
  representation=Protobuf<MyConfig>,
  path="config/my_config.textproto",
);
connect config_fallback to my_config.fallback;

new my_cog: MyCog;

connect my_config to my_cog.config;
```

Here we attempt to load config from the log, and fall back to a file if no snapshot is found.

## Upgrading schemas

When loading snapshots of either state or config from a log, normal Clockwork schema upgrades will be performed.
When loading from a file, the data must be compatible with the current schema.

## Advanced Usage

### Multiple State Snapshots

You can snapshot multiple states at different intervals:

```clockwork
box MyBox
{
  new fast_state: State(representation=FastState, memory_resource=memory);
  new slow_state: State(representation=SlowState, memory_resource=memory);
  new my_cog: MyCog;

  connect fast_state to my_cog.fast_state;
  connect slow_state to my_cog.slow_state;

  // Snapshot fast state frequently
  apply TakeSnapshots(channel=FastStateChannel, interval=100ms) to my_cog.fast_state;

  // Snapshot slow state less frequently
  apply TakeSnapshots(channel=SlowStateChannel, interval=10s) to my_cog.slow_state;
}
```
