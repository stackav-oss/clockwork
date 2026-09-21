# Clockwork system topology tool

Clockwork systems define topology through connections made within each Box in the system.
Users can visually inspect the files with these boxes to see these connections, but as systems grow larger this quickly becomes unfeasible.
The topology tool provides an easy way to extract basic information about the system.
Examples include describing the publishers and subscribers of a channel or the inputs and outputs of an entity (e.g., Cog or Socket).
Please see `--help` both on the tool and each sub-command for the latest information on how to use the tool.

## Example usage

Use of the tool first requires a defined `topology_summary` target using a Clockwork system target as an input.
Here is an example of defining such a target.

```bazel
load("//clockwork/tools/topology:rules.bzl", "topology_summary")

clk(
    name = "test_system_clk",
    srcs = ["test_system.clk"],
    outs [...],
)

topology_summary(
    name = "test_system_clk_topology",
    system_target = ":test_system_clk",
)
```

In this example, the target `:test_system_clk` is assumed to be a Clockwork target that defines a `system_target` within the `.clk` source file.
Assuming the example above is in package `//a/b/c`, below is an example invocation of the tool.

```bash
bazel run @clockwork//clockwork/tools/topology:query -- //a/b/c:test_system_clk_topology list-entities
```

Internally, the tool calls `bazel` to build that topology target ensuring it is up to date.
This specific invocation will list the names of every entity in the system.

For more details, there are some unit tests that exercise this tool with a sample system and a topology target.
Those tests can be found within `//clockwork/tools/topology/tests/...`.

### FAQ

#### Bridge impact

A common question often asked is whether adding an input to a cog will cause a change in network utilization.
By running the `channel` subcommand, users can sees which CPU the publisher and subscribers run on.

```console
$ bazel run @clockwork//clockwork/tools/topology:query -- @clockwork//clockwork/tools/topology/tests:test_system_multi_node_topology_summary channel MultiNodeChan
...
Channel: MultiNodeChan
  Size: 9 messages (72 bytes)
  Type: @clockwork::clockwork::tests::support::test_messages_multi_node.MultiNodeMessage
  Message Size: 8 bytes
  Publishers:
    - CPU: TestSystemCpu1
        - @clockwork::clockwork::tests::support::test_system_multi_node.test_system_multi_node.test_cogs_box_1.source_cog
  Subscribers
    - CPU: TestSystemCpu2
        - @clockwork::clockwork::tests::support::test_system_multi_node.test_system_multi_node.test_cogs_box_2.sink_cog
  Routes:
    - TestSystemCpu1 -> TestSystemCpu2: tcp (Port: 1111)
```

The `Routes` section shows the bridge type and connection details for channels that cross CPU boundaries:

- For TCP bridges: displays the port number


Channels with multiple subscribers on different CPUs will show multiple routes:

```console
$ bazel run @clockwork//clockwork/tools/topology:query -- @clockwork//clockwork/tools/topology/tests:test_system_multi_route_topology_summary channel MultiRouteChan
...
Channel: MultiRouteChan
  Size: 9 messages (72 bytes)
  Type: @clockwork::clockwork::tests::support::test_messages_multi_node.MultiNodeMessage
  Message Size: 8 bytes
  Publishers:
    - CPU: MultiRouteCpu1
        - @clockwork::clockwork::tests::support::test_system_multi_route.test_system_multi_route.source_box.source_cog
  Subscribers
    - CPU: MultiRouteCpu2
        - @clockwork::clockwork::tests::support::test_system_multi_route.test_system_multi_route.sink_box_1.sink_cog
    - CPU: MultiRouteCpu3
        - @clockwork::clockwork::tests::support::test_system_multi_route.test_system_multi_route.sink_box_2.sink_cog
  Routes:
    - MultiRouteCpu1 -> MultiRouteCpu2: tcp (Port: 2000)
    - MultiRouteCpu1 -> MultiRouteCpu3: tcp (Port: 2000)
```

If you are unsure of the exact channel name to query for, simply list them out first.

```console
$ bazel run @clockwork//clockwork/tools/topology:query -- @clockwork//clockwork/tools/topology/tests:test_system_multi_node_topology_summary list-channels
...
MultiNodeChan
SinkChan
SourceChan
```

The `list-channels` command can filter logged channels and show the CPUs where
the selected log receives each channel:

```console
$ bazel run @clockwork//clockwork/tools/topology:query -- TOPOLOGY_TARGET list-channels --logged-telemetry
Channel: TelemetryChan
  Telemetry log:
    - CPU: Cpu1
```

The logging filters are mutually exclusive:

- `--logged-telemetry` lists all telemetry-logged channels.
- `--redundant-telemetry` lists telemetry channels with redundant logging.
- `--non-redundant-telemetry` lists telemetry channels without redundant logging.
- `--logged-events` lists all event-logged channels.

Redundant log locations are marked with `(redundant)`.
A channel queried with the `channel` command shows both its event and telemetry
log locations.
The logging filters also include logged multi-producer channels, which remain
omitted from the unfiltered topology channel list.

#### State and memory resource introspection

In addition to channels, entities can be connected to states and memory resources.
The `memory` and `state` commands allow for introspection into those entities.

The `entity` command shows the connected entities:

```console
$ bazel run @clockwork//clockwork/tools/topology:query -- @clockwork//clockwork/tools/topology/tests:test_system_multi_node_topology_summary entity @clockwork::clockwork::tests::support::test_system_multi_node.test_system_multi_node.test_cogs_box_2.sink_cog
...
Entity: @clockwork::clockwork::tests::support::test_system_multi_node.test_system_multi_node.test_cogs_box_2.sink_cog
  Process: @clockwork::clockwork::tests::support::test_system_multi_node.test_system_multi_node.test_process_2
  CPU: TestSystemCpu2
  Inputs:
    - MultiNodeChan
  Outputs:
    - SinkChan
  States:
    - @clockwork::clockwork::tests::support::test_system_multi_node.test_system_multi_node.test_cogs_box_2.sink_state
  Memory Resources:
    - @clockwork::clockwork::tests::support::test_system_multi_node.test_system_multi_node.test_cogs_box_2.sink_memory
```

To see how much memory the memory resource can use, use the `memory` command:

```console
$ bazel run @clockwork//clockwork/tools/topology:query -- @clockwork//clockwork/tools/topology/tests:test_system_multi_node_topology_summary memory @clockwork::clockwork::tests::support::test_system_multi_node.test_system_multi_node.test_cogs_box_2.sink_memory
...
Memory Resource: @clockwork::clockwork::tests::support::test_system_multi_node.test_system_multi_node.test_cogs_box_2.sink_memory
  Type: HeapMemory
  Size: 1000000 bytes
  Entities:
    - @clockwork::clockwork::tests::support::test_system_multi_node.test_system_multi_node.test_cogs_box_2.sink_cog
  States:
```

States can also use memory resources when they are defined externally:

```console
$ bazel run @clockwork//clockwork/tools/topology:query -- @clockwork//clockwork/tools/topology/tests:test_system_multi_node_topology_summary state @clockwork::clockwork::tests::support::test_system_multi_node.test_system_multi_node.test_cogs_box_2.sink_state
...
State: @clockwork::clockwork::tests::support::test_system_multi_node.test_system_multi_node.test_cogs_box_2.sink_state
  Extern: Yes
  Type: SinkState
  Memory Resource: @clockwork::clockwork::tests::support::test_system_multi_node.test_system_multi_node.test_cogs_box_2.state_memory
  Entities:
    - @clockwork::clockwork::tests::support::test_system_multi_node.test_system_multi_node.test_cogs_box_2.sink_cog
```
