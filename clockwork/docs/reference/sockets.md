# Clockwork support for sockets

Clockwork provides support for sockets that can be connected to channels to avoid cogs needing to do network IO manually.
Sockets are instantiated with address, port, message type, and the direction of the IO (incoming vs outgoing).
Each socket can then be connected to a channel inside a box in the same way cogs are connected.

## UDP sockets

Clockwork provides a `udp_socket` type to either read `incoming` UDP packets from a socket and publish to a channel or subscribe to `outgoing` UDP packets and write them to a socket.
The following sections describe how to instantiate a `udp_socket`, connect it to a channel, and set it up to run in a running process.

### Declaring a `udp_socket`

First, declare the message type you want on the socket.
For this example we declare it inline, but this can be in separate Clockwork module as well.

```clockwork
// Payload type
schema Payload
{
  fields
  {
    // Some field
    #0 value: UInt32;
  }
}
```

Now set up a `udp_socket` using this payload type.
This example will construct an `incoming` socket which will read packets from a UDP socket and publish them to a channel.

```clockwork
// UDP socket.
udp_socket IncomingUdp
{
  address: 127.0.0.1;
  port: 12345;
  direction: incoming;
  message_type: Tachyon<Payload>;
}
```

When reading or writing the `message_type` to a socket, the `Tachyon` representation is expected to match the packet's binary format.
This includes the size of any incoming packets - any mismatches will cause the cog to fail.
The payload of each packet will be serialized and deserialized directly from the Tachyon representation.

Representing the UDP packet payload as a Tachyon type may not be useful in all cases, so Clockwork can also provide direct access to the UDP packet payloads.
See the later section about the [VarPacket type](#special-varpacket-type) for more details.

Finally, make sure to instantiate the `udp_socket` in the `cpp_target` to generate the corresponding c++ class.
This enables use of the `udp_socket` in a `box` and a `casing`.

```clockwork
cpp_target udp_example
{
    options
    {
        namespace clockwork::testing;
    }

    schema Payload;
    representation Tachyon<Payload>;
    interface Tap<Tachyon<Payload>>;

    udp_socket IncomingUdp;
}
```

### Socket options

You can set a small selection of socket options from `socket(7)` by filling in the `options` block in a `udp_socket` declaration.
See the table below for a list of options you can set.

| Option Name         | socket(7) Option  | Value Type           | Description                                                                                                                                        |
| ------------------- | ----------------- | -------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------- |
| `reuse_address`     | `SO_RESUSEADDR`   | Boolean              | If true, tell the kernel to allow reuse of the local address by other sockets.                                                                     |
| `receive_buffer`    | `SO_RCVBUF`       | Unit literal (bytes) | Tell the kernel how many bytes to allocate for the socket's receive buffer.                                                                        |
| `bind_to_interface` | `SO_BINDTODEVICE` | Boolean              | If true, tell the kernel not to route messages on egress, but instead to send them directly from the interface associated to the socket's address. |

Consider the following example.
It will create a bidirectional UDP socket with a receive buffer of 4096 bytes.
It will only send packets out whichever network interface has the address `10.100.0.20` and other sockets are free to bind to `10.100.0.20:1234`.

```clockwork
udp_socket WithOptions
{
  address: 10.100.0.20;
  port: 1234;
  direction: bidirectional;
  message_type: Tachyon<var_packet::VarPacket<4>>;
  remote_address: 10.100.0.10;
  remote_port: 5678;

  options
  {
    reuse_address: true;
    receive_buffer: 4096byte;
    bind_to_interface: true;
  }
}
```

### Multicast

You can use `multicast_udp_socket` to create UDP sockets that send and receive over multicast.

Consider the following snippet.

```clockwork
// A multicast receiver.
multicast_udp_socket IncomingMulticast
{
  group_address: 239.22.0.1;
  interface_address: 10.0.1.100;
  port: 5000;
  direction: incoming;
  message_type: Tachyon<Payload>;
}

// A multicast sender.
multicast_udp_socket OutgoingMulticast
{
  group_address: 239.22.0.2;
  interface_address: 10.0.1.100;
  port: 9000;
  direction: outgoing;
  message_type: Tachyon<Payload>;
}

// A bidirectional multicast socket.
multicast_udp_socket BidirectionalMulticast
{
  group_address: 239.22.0.3;
  interface_address: 10.0.1.100;
  port: 9001;
  remote_port: 9002;
  direction: bidirectional;
  message_type: Tachyon<Payload>;
}
```

The socket named `IncomingMulticast` will receive all UDP packets destined to the group `239.22.0.1:5000` via whichever network interface has the address `10.0.1.100`.
Note that `IncomingMulticast` can safely coexist with a unicast socket bound to `10.0.1.100:9000`, since the multicast socket only uses that address for routing purposes.
Similarly, the socket named `OutgoingMulticast` will send packets to the group `239.22.0.2:9000` via whichever interface has `10.0.1.100`.

The socket named `BidirectionalMulticast` will receive packets on `239.22.0.3:9002` and will send packets with a source of `10.0.1.100:9001`.
This socket can safely coexist with a unicast socket bound to `10.0.1.100:9001` because the underlying socket is only bound to the multicast group address; the kernel transparently swaps in the interface address for transmissions.

Other than `address` and `remote_address`, `multicast_udp_socket` has all the same fields as `udp_socket`.

### Using a `udp_socket` in a running system

To add the `udp_socket` to a running system, follow the same pattern as Cogs.
The `message_type` for the `channel` and the `udp_socket` must match.

```clockwork
// Some channel
channel Channel
{
    message_type: Tachyon<Payload>;
    max_num_messages: 100;
}

box Box
{
    new udp_socket: IncomingUdp;
    connect udp_socket to Channel;
}
```

Don't forget to also instantiate the `udp_socket` in a casing. (e.g. `DemoUdpCasing` below).

The above example is for `incoming` UDP packets.
The `udp_socket` type also supports `outgoing` for the `direction`.
When using `outgoing`, the socket type subscribes to a channel and writes to the socket.
To connect an `outgoing` socket, reverse the `connect` clause in the `box` similar to the order of a `cog` subscriber.

Finally, the complete `cpp_target` would look like the following.

```clockwork
cpp_target udp_example
{
    options
    {
        namespace clockwork::testing;
    }

    schema Payload;
    representation Tachyon<Payload>;
    interface Tap<Tachyon<Payload>>;

    udp_socket IncomingUdp;

    casing DemoUdpCasing
    {
      udp_socket IncomingUdp;
    }

    process DemoUdpProc
    {
      casing DemoUdpCasing;
      box Box;
    }
}
```

### Special `VarPacket` type

The `message_type` of a `udp_socket` can be any schema.
Each message is written to or read form the socket in the `Tachyon` representation as if mem-copying to/from another buffer.
This works great if both sides of the socket use Clockwork or if the `Tachyon` representation matches the packet exactly.
If this is not the case, Clockwork special cases the `udp_socket` read/write behavior for `platforms::clockwork::io::var_packet::VarPacket`.

The `VarPacket` schema has a single field `bytes` which is a `VarArray` of bytes and is parameterized on the size of the `bytes` container.
On writes, only the bytes in `bytes` are written to the socket.
On reads, the incoming packet is written to `bytes` and it is resized appropriately.
If an incoming packet's payload is larger than the `max_size_bytes`, the cog will fail.
Using `VarPacket` requires more work in the Cog to write data in binary form, but it allows any packet structure (even variable size / layout) as long as it has a static bound on the payload size.

To use `VarPacket`, simply replace the `message_type` field with `var_packet::VarPacket<max_size_bytes=...>` on both the `udp_socket` and the `channel` then add a `use @clockwork::clockwork::io::var_packet` statement to the top of the `.clk` file.

### Batching packets

Incoming udp sockets support reading in batches.
Users can specify the size (`n`) of a batch by specifying `batch_size: n;`.
For high rate packet, batching can be more efficient to avoid the event loop spinning.
However, in the current implementation a batch will only be published once full.

> [!IMPORTANT]
> This means if the incoming packets stop, then a partial batch will effectively be dropped.
> In the future a timer to force publish a partial batch will be implemented.
