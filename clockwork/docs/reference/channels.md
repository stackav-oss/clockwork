# Clockwork channels

## Channel Types

Channels support multiple channel types using the `channel_type:` attribute. For example:

```clockwork
channel GpuInput
{
  message_type: Tachyon<Message>;
  max_num_messages: 10;
  channel_type: gpu;
}
```

The allowed values are:

- `unspecified` - Default. Allows the implementation to use any channel type
- `shared_memory` - Specifies that shared memory channels should be used
- `gpu` - Specifies that a channel with GPU mapped memory should be used


### Device pointers

Certain channel types may use or support access to on device memory.
This is managed through the `DevicePtr<T>` type.

It operates similarly to a `unique_ptr<T>`: it supports `.get()` to access the pointer, `operator->` to access members, and cannot be copied.

Inputs [require a `use_device_ptr: true` configuration](../concepts/input_views_triggers.md#device-pointers) and calling `device_ptr(iter)` will transfer ownership of the underlying resource to the user code.
Further calls to `device_ptr(iter)` will return `nullptr` for a particular `iter`.

Outputs don't require additional configuration and support `device_ptr([index])`, however note that the return may be null if the channel doesn't support device pointer access.

Example:

```cpp
  auto dev_input = dial.get_inputs().get_in_a().device_ptr(dial.get_inputs().get_tensor().get_latest_new_msg());
  auto dev_output = dial.get_outputs().get_tensor().device_ptr();

  assert(dev_input && dev_output);
  on_device_memcpy(dev_output->get_mutable_data(), dev_input->get_data(), sizeof(Tappy<Message>));

  dial.get_outputs().get_out_a().mark_for_publish();
```
