# TCP bridge

The TCP bridge allows us to glue together shared memory buffers on hosts connected by an IP network.
This document describes the protocol the bridge uses to achieve this.

From the point-of-view of a channel, the TCP bridge has two parts: client and server.
The consumer of a channel uses the bridge client to receive messages exported by a remote bridge server, which is collocated with the producer.
A bridge server can deliver messages to multiple clients.

Once a client connects to a server, the server will forward messages from its underlying Pinion buffer in three parts: header, message, and tail.
The header indicates the size of the message and has some metadata copied from the Pinion header of the original message.
The tail includes a flag that will be set to true if the client should commit the payload it just received (or false if it should discard).
If the client ends up committing the message, it will copy the metadata from the header to ensure a consistent view of the channel between it and the server.
