# Channel Spy

The channel spy provides an API in C++ and python to spy read messages from channels in a live clockwork system.
The channel spy works by examining the pinion buffers in shared memory and uses the cursors to find new messages.
The channel spy configuration is stored in the same shared memory directory as the pinion buffers.
Because the spy is reading from shared memory, it is only able to see channels that are published on the local machine.

## Channel Spy API

The channel spy API is implemented in C++ with nanobind wrappers to provide the python API.
The most common use case is to subscribe to a channel and do something with the messages as they are received.
The interface also provides for subscribing to receive raw message data and to query the channels that are available on the local machine.

### C++

The C++ API is implemented by the ChannelSpy class.
Callers should use the default constructor to spy on a live clockwork system.

#### Channels

The channels method returns the metadata for each channel that can be spied on the local machine.
Channel metadata contains the name, type, and clockwork schema definition for each channel.

```cpp
stack::clockwork::tools::ChannelSpy channel_spy{};
const auto channels = spy.channels();
<PROCESS_CHANNELS>;
```

#### Deserialized Message Callbacks

The templated subscribe method creates a subscription for a channel that invokes a callback with a deserialized message for each message it spies on the channel.
The template parameter must by a 'Tap Tachyon' (aka 'Tappy') type.

```cpp
stack::clockwork::tools::ChannelSpy channel_spy{};
channel_spy.subscribe<example::ClassTap>(
    [](uint64_t seqno, int64_t msg_time, const auto& message){ <PROCESS_MESSAGE>; });
channel_spy.run();
```

#### Raw Message Callbacks

The non-templated subscribe method creates a subscription to receive callbacks with raw message data.
The data is passed as a span of bytes for the actual message storage in the pinion buffer.
The callback provides an overrun check function that can be used to check whether the data in the buffer is still valid.
Raw callbacks should be used with caution.

```cpp
stack::clockwork::tools::ChannelSpy channel_spy{};
channel_spy.subscribe<example::ClassTap>(
    [](uint64_t seqno, int64_t msg_time, const message_data, const auto& overrun_check_fn){
        <DO_SOMETHING_WITH_MESSAGE>;
        if (!overrun_check_fn())
        {
          <DISCARD_WHAT_YOU_DID>;
          return;
        }
        <KEEP_WHAT_YOU_DID>;
    });
channel_spy.run();
```

### Python

The python API is implemented by the ChannelSpy class in "py_channel_spy.py".
Callers should use the default constructor to spy on a live clockwork system.

#### Channels

```python
import platforms.clockwork.tools.channel_spy.py_channel_spy as channel_spy

spy = channel_spy.ChannelSpy()
<PROCESS_LIST_OF_CHANNEL_NAMES>
```

#### Deserialized Message Callbacks

The subscribe_typed method takes the message type as a parameter and passes a deserialized instance of that type to the callback.

```python
import platforms.clockwork.tools.channel_spy.py_channel_spy as channel_spy
from platforms.clockwork.tools.channel_spy.tests.support import test_message_clk_nb as test_message_clk

spy = channel_spy.ChannelSpy()

def callback(sequence_number: int, message_time: int, message: test_message_clk.TestMessage) -> None:
    <PROCESS_MESSAGE>

spy.subscribe_typed("channel_1", test_message_clk.TestMessage, callback)
spy.run()
```

#### Automatic Wrapper Generation

The subscribe_auto method is similar to subscribe_typed except that the wrapper class type is generated automatically using schema definition from the channel spy configuration file.

```python
import platforms.clockwork.tools.channel_spy.py_channel_spy as channel_spy

spy = channel_spy.ChannelSpy()

def callback(sequence_number: int, message_time: int, message: object) -> None:
    <PROCESS_MESSAGE>

spy.subscribe_typed("channel_1", typed_callback)
spy.run()
```

#### Raw Message Callbacks

The subscribe_raw method subscribes to receive callbacks with the raw message data passed in a bytearray.

```python
import platforms.clockwork.tools.channel_spy.py_channel_spy as channel_spy

spy = channel_spy.ChannelSpy()

def raw_callback(sequence_number: int, message_time: int, message_data: bytearray) -> None:
    <PROCESS_RAW_MESSAGE>

spy.subscribe_raw("channel_1", raw_callback)
spy.run()
```

## Channel Spy CLI

The channel spy CLI is a python script for spying on channels on the vehicle and on a local workstation.
The channel spy CLI can spy on any running clockwork system including simulations running locally.
On the vehicle the command to run channel is "./debug_tools/channel_spy".
On a local workstation the command to run channel spy is "stack spy".

### List Channels

The list-channels subcommand displays the names of the channels that can be spied.

```bash
$ stack spy list-channels

/example_channel1
/example_channel2
/example_channel3
/example_channel4
...
```

### echo

The echo subcommand subscribes to a channel and dumps the messages it receives to standard output in JSON format.

```bash
$ stack spy echo /log/gnss/position

/example_channel1: 1732127452098646417 [141]
{
  "time_of_validity_ns": 1732127452098639729,
  "latitude_deg": 40.1824407,
  "longitude_deg": -79.575818,
  "height_m": 364.612,
  "horizontal_stdev_m": 0.80565,
  "height_stdev_m": 1.13652,
  "num_satellites_solution": 15,
  "is_valid": true
}
```

## Channel Spy Configuration

In order for the channel spy tool to work it needs to have the channel spy configuration file under the shared memory directory where the files for the pinion buffers are stored.
The clockwork compiler generates a channel spy configuration file for each CPU domain found under the clockwork system target definition.
A cog is provided to write the channel spy configuration file for each CPU domain at system startup.
The following is an example of how to create a box that can be added to a system target to initialize the channel spy configuration file.

```python
use @clockwork::clockwork::tools::channel_spy::channel_spy_config::ChannelSpyConfig
use @clockwork::clockwork::tools::channel_spy::channel_spy_config_init::SpyConfigInitCog
use @clockwork::clockwork::tools::channel_spy::channel_spy_config_init::spy_config_init_exe
use path::to::your::directory::cpu_domains

box Cpu1SpyConfigInit
{
  new memres: HeapMemory(max_size=10'000'000);
  new spy_config: SerializedDataFile(representation=Tachyon<ChannelSpyConfig<>>, path="path/to/generated/config/system_prefix.Cpu1_channel_spy_config.tachyon");
  new spy_config_init_cog: SpyConfigInitCog;

  connect memres to spy_config_init_cog.memres;
  connect spy_config to spy_config_init_cog.spy_config;
}

box DemoSpyConfigInit
{
  new cpu1_spy_config_box: Cpu1SpyConfigInit;
  new cpu1_spy_config_proc: Process(executable=spy_config_init_exe);
  apply HostProcess(process=cpu1_spy_config_proc) in cpu1_spy_config_box;
  apply HostCpuDomain(cpu_domain=cpu_domains::Cpu1) to cpu1_spy_config_proc;
}
```
