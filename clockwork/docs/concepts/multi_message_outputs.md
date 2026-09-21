# Clockwork multi-message outputs

This document details the semantics of Clockwork outputs that can publish more than one message in a single Cog execution.
This is useful for Cogs that naturally produce a small batch of same-type messages at the same time, such as a detector that emits several observations, a splitter that fans one input into several output records, or a batch-processing Cog that drains several work items on one execution.

The core idea is simple: an output endpoint may declare a bounded number of writable message slots per execution.
On each execution, the Cog may publish any subset of reserved slots starting from the beginning without skipping.

> [!TIP] > `max_msgs_per_exec` determines how many output messages Clockwork reserves for one execution.
> `mark_for_publish(k)` commits the first `k` reserved messages to become visible to subscribers.

## TL;DR

By default, every output is a single-message output.
You do not need to write anything special for that case:

```clockwork
outputs
{
    result: Tappy<ResultMsg>;
}
```

To publish up to _N_ messages from the same output endpoint in one execution, set `max_msgs_per_exec` on that output:

```clockwork
outputs
{
    detections: Tappy<DetectionMsg>
    {
        max_msgs_per_exec: 4;
    }
}
```

Then write up to four output messages and publish the count you actually produced:

```cpp
void execute_cog(DetectorDial& dial)
{
  auto& detections = dial.get_outputs().get_detections();

  detections.message(0).score = 0.98;
  detections.message(1).score = 0.87;

  detections.mark_for_publish(2);
}
```

Subscribers observe exactly two messages from this execution.
The remaining reserved slots are discarded.

## Output multiplicity

A Clockwork channel is still an ordered stream of messages.
Multi-message output does not create a nested batch message, a second channel, or a special subscriber-side view.
It only allows one Cog execution to append more than one message to the output channel.

If an output has:

```clockwork
max_msgs_per_exec: N;
```

then each execution may publish any count _k_ where `0 <= k <= N`.

Clockwork reserves _N_ writable slots before calling the Cog.
The Cog fills whichever slots it needs, then calls `mark_for_publish(k)`.
The first _k_ slots are committed to the channel in order.
Slots from _k_ through `N - 1` are discarded.

> [!IMPORTANT]
> If `k` messages are published, it can only be the first `k` messages in the reservation without skipping.
> If you call `mark_for_publish(2)`, subscribers receive `message(0)` and `message(1)`.
> There is no way to publish only `message(1)` and `message(3)` because it skips `message(0)` and `message(2)`.

## Declaring multi-message outputs

Use `max_msgs_per_exec` inside an output block:

```clockwork
cog Example
{
    outputs
    {
        output1: Tappy<TestOutputMessage>
        {
            max_msgs_per_exec: 4;
        }
    }

    execution
    {
        execute when: init;
    }
}
```

The value must be greater than one.
Omit `max_msgs_per_exec` for the default single-message behavior.

> [!NOTE]
>
> `max_msgs_per_exec: 1` is intentionally a compile error.
> Single-message output is expressed by omitting `max_msgs_per_exec` from the output block.

`max_msgs_per_exec` is orthogonal to `connect_optional`.
An output may be optionally connected, multi-message, both, or neither.

```clockwork
outputs
{
    maybe_detections: Tappy<DetectionMsg>
    {
        connect_optional: true;
        max_msgs_per_exec: 8;
    }
}
```

## C++ Cog API

For a single-message output, the generated Dial API provides non-indexed methods `message()` and `mark_for_publish()`:

```cpp
auto& output = dial.get_outputs().get_result();
output.message().value = 42;
output.mark_for_publish();
```

For a multi-message output, the generated output type is of the form `clockwork::pinion::Publishable<Message, N>`.
Use indexed methods to write the reserved slots:

```cpp
auto& output = dial.get_outputs().get_detections();

for (size_t index = 0; index < scores.size(); ++index)
{
  output.message(index).score = scores[index];
}

output.mark_for_publish(scores.size());
```

The most common methods are:

- `size()` - returns the declared capacity `N`.
- `message(index)` - returns a mutable reference to slot `index`, or throws on out-of-bounds access.
- `messages()` - returns an array of mutable message pointers.
- `mark_for_publish(k)` - publishes the first `k` messages.
- `mark_all_for_publish()` - publishes all reserved messages.
- `connected()` - reports whether the output is connected to any channel.

The no-argument `message()` and no-argument `mark_for_publish()` methods are only available on single-message outputs.
For multi-message outputs, use `message(index)` and `mark_for_publish(k)`.

### Publishing zero messages

It is valid to publish zero messages:

```cpp
auto& output = dial.get_outputs().get_detections();
output.mark_for_publish(0);
```

This commits nothing to the channel.
It is equivalent to deciding not to publish from that endpoint during this execution.

### Publishing all messages

If every reserved slot was populated, use `mark_all_for_publish()`:

```cpp
auto& output = dial.get_outputs().get_detections();

for (size_t index = 0; index < output.size(); ++index)
{
  output.message(index).score = scores[index];
}

output.mark_all_for_publish();
```

This is equivalent to `mark_for_publish(output.size())`.

### Repeated publish calls

Calling `mark_for_publish(k)` more than once on the same output in one execution is allowed, but discouraged.
If the calls use the same value, it is as if it had only been called once.
If the calls use different values, Clockwork uses the largest value and logs a non-fatal error.

```cpp
output.mark_for_publish(1);
output.mark_for_publish(2);  // Two messages are published; an error is logged.
```

This behavior keeps the channel state conservative: once a slot has been marked for commit, a later call cannot unpublish it.

> [!WARNING]
> Avoid using repeated calls as ordinary control flow.
> Compute the count `k` you intend to publish, write messages `0` to `k-1`, and call `mark_for_publish(k)` once.

### Counts larger than capacity

If `mark_for_publish(k)` is called with `k > N`, Clockwork publishes all `N` messages.

## Python Cog API

For Python-backed Cogs, a multi-message output is passed to Python with one writable buffer per output slot.
The generated Python output object provides a `publish(...)` method that serializes into the next slot and advances the output's publish count.

As an example, here is a Python Cog implementation that can publish multiple detections by calling `publish(...)` once for each message it wants to emit:

```python
from clockwork.example.detector.detector_clk_py_dial import DetectorDial
from clockwork.example.detector.detection_msg_clk_py import DetectionMsg


class DetectorImpl:
    """Python implementation for a detector Cog."""

    @classmethod
    def execute_cog(cls, dial: DetectorDial) -> None:
        """Run one Cog execution."""
        for detection in dial.inputs.raw_detections.new_msgs_view:
            output_msg = DetectionMsg()
            output_msg.track_id = detection.message.track_id
            output_msg.score = detection.message.score

            dial.outputs.detections.publish(output_msg)
```

If the `detections` output declared `max_msgs_per_exec: 4`, the example above may call `dial.outputs.detections.publish(...)` up to four times in one execution.
The generated output object exposes the reserved writable slots as `dial.outputs.detections.messages`, but most Cog code should prefer `publish(...)` because it also updates the count that the bridge uses when committing messages.

For single-message outputs, the bridge still uses the existing `is_published` behavior.
For multi-message outputs, the bridge looks for an integer `publish_count` attribute instead.
The generated Python output class internally maintains that value when `publish(...)` is called.

## What subscribers observe

Subscribers do not see a special "batch" object.
They see ordinary messages appended to the channel in order.

For example, suppose an output has `max_msgs_per_exec: 4`, and the Cog executes three times:

- Execution 1 calls `mark_for_publish(2)`.
- Execution 2 calls `mark_for_publish(0)`.
- Execution 3 calls `mark_for_publish(3)`.

The output channel receives five messages total:

```text
E1.message(0), E1.message(1), E3.message(0), E3.message(1), E3.message(2)
```

Input views and execution conditions on downstream Cogs work with those messages exactly as they work with any other messages.
If a downstream Cog needs to process every message, size its input view and `new_message(..., max=...)` condition accordingly.

See [Clockwork channels, input views, and conditions](input_views_triggers.md) for the subscriber-side behavior.

## Channel sizing

`max_msgs_per_exec` increases the maximum burst size on the output channel.
Make sure the channel has enough slots for the largest burst you may publish and for the downstream consumers' input views.

```clockwork
channel DetectionChannel
{
    message_type: Tachyon<DetectionMsg>;
    max_num_messages: 32;
}
```

A Cog that publishes up to four messages per execution can consume four channel slots at once.
If the channel is too small, older messages may be displaced faster than subscribers can observe them.

The output's `max_msgs_per_exec` is also recorded in the Pinion buffer layout and propagated through generated process descriptions and bridge configuration.
Clockwork uses it when deciding how much overrun margin a subscriber needs, because one producer execution may advance the channel by more than one message.

> [!WARNING]
> A multi-message output makes burst rate important.
> Size the channel and downstream input views for the maximum number of messages that can arrive between subscriber executions.

## Optional connections

When `connect_optional` is configured, if a multi-message output is not connected, Clockwork can still provide a publishable object to the Cog, but publishing is a no-op for that endpoint.
The Cog can check `connected()` when it wants to skip expensive output work:

```cpp
auto& output = dial.get_outputs().get_detections();
if (!output.connected())
{
  return;
}
```

## Choosing `max_msgs_per_exec`

Choose the smallest bound that covers the Cog's real per-execution burst.
This value is part of the Cog definition's contract with the runtime and with downstream consumers.

Good candidates for multi-message output are Cogs with a known, small maximum output count.

Poor candidates are Cogs with naturally unbounded output:

- Draining an arbitrary queue until empty.
- Emitting one message per element of an unbounded input list.
- Using the channel as a general-purpose log stream.

For those cases, either put the repeated data inside one schema message with an explicit bounded container, or redesign the Cog so each execution has a real bounded output contract.

## Common patterns

### Filtering messages

When generating outputs from an input loop, fill the output slots sequentially and stop at capacity:

```cpp
auto& output = dial.get_outputs().get_filtered_items();
size_t count = 0;

for (const auto& item : dial.get_inputs().get_items().get_new_msgs_view())
{
  if (!should_publish(item))
  {
    continue;
  }

  if (count == output.size())
  {
    break;
  }

  output.message(count).value = item.value;
  ++count;
}

output.mark_for_publish(count);
```

### Publish at most one message

If the output is conceptually at most one message per execution, do not use `max_msgs_per_exec`.
Use the default single-message API:

```clockwork
outputs
{
    status: Tappy<StatusMsg>;
}
```

```cpp
auto& status = dial.get_outputs().get_status();
status.message().state = State::ready;
status.mark_for_publish();
```

## Summary

Multi-message output is a bounded burst-publishing contract:

- The Cog definition declares the capacity with `max_msgs_per_exec`.
- Clockwork reserves exactly that many writable output slots per execution.
- The Cog writes a subset of those slots starting from the beginning without skipping.
- `mark_for_publish(k)` commits exactly the first `k` messages.
- Subscribers observe ordinary channel messages in order.
- Omitting `max_msgs_per_exec` uses the default single-message behavior.
