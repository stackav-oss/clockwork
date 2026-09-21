# Clockwork multi-connect inputs

This document details the semantics of Clockwork inputs that can be connected to an array of channels.
This is useful for Cogs that need to receive from multiple senders that all send the same message type.

By default, inputs can only be connected to a single channel.
You do not need to write anything special for that case.

## Example

This example shows a cog with three inputs, all with the same type, that executes when a message is received on any of the inputs.

```clockwork
cog ExampleCog
{
  inputs
  {
    input1: Tappy<InputMsg>;
    input2: Tappy<InputMsg>;
    input3: Tappy<InputMsg>;
  }
  execution
  {
    condition new_input1: new_message(input1);
    condition new_input2: new_message(input2);
    condition new_input3: new_message(input3);
    execute when: new_input1 or new_input2 or new_input3;
  }
}
```

Use the `multi_connect` option to group these inputs into an array.

```clockwork
cog ExampleCog
{
  inputs
  {
    input: Tappy<InputMsg>
    {
        multi_connect: 3;
    }
  }
  execution
  {
    condition new_input: new_message(input);
    execute when: new_input;
  }
}
```

Channels are connected to `multi_connect` inputs by connecting an array of channels to the input in the box definition.

```clockwork
box ExampleBox
{
  new example_cog: ExampleCog;
  connect [
    Channel1,
    Channel2,
    Channel3,
  ] to example_cog.input;
}
```

The `new_message` condition on a `multi_connect` input evaluates to true if a new message is available on any of the input channels.
The `get_input` method on the dial returns an array of reference wrappers for the input channel elements.

```cpp
void execute_cog(DetectorDial& dial)
{
  for (const auto& input_ref: dial.get_inputs.get_input())
  {
    for (const auto& msg: input_ref.get().get_new_msgs_view())
    {
      // Process the message
    }
  }
}
```

## View parameters

The `multi_connect` option can be combined with any of the other parameters options such as `max_msgs` or `manual_cursor` except `no_dial`.
The view parameters on a `multi_connect` channel are applied to every channel in the array.

Combining `multi_connect` with `connect_optional` allows fewer channels to be connected than the `multi_connect` value.

## Unit test cogs

Unit test cogs support the `multi_connect` option by returning an array of input publishers from the get method for a `multi_connect` channel.
For example, the following code could be used to send messages to the `multi_connect` channel in the previous example.

```cpp
ExampleCogTestWrapper example_cog;
example_cog.initialize(time0);

// Initialize some messages

auto inputs = example_cog.get_inputs().get_input();
inputs[0].publish(message0, time1);
inputs[1].publish(message1, time2);
inputs[2].publish(message2, time3);
exec_result = example_cog.execute(time4);
```

## Limitations

- Aligners do not support `multi_connect` channels.
- Cog parameters cannot be used to set values within the view parameters section, including the `multi_connect` value.
- Channels defined with `publishers: multiple` cannot be connected to `multi_connect` channels.
