# TCP bridge

The TCP bridge allows us to glue together shared memory buffers on hosts connected by an IP network.
This document describes the protocol the bridge uses to achieve this.

From the point-of-view of a channel, the TCP bridge has two parts: client and server.
The consumer of a channel uses the bridge client to receive messages exported by a remote bridge server, which is collocated with the producer.
A bridge server can deliver messages to multiple clients.

Once a client connects to a server, the server will forward messages from its underlying Pinion buffer in three parts: header, message, and tail.
The header indicates the size of the message and has some metadata copied from the Pinion header of the original message.
When the client published the message on the destination side, it will copy the metadata from the header to ensure a consistent view of the channel between it and the server.

By default, the bridge tries to deliver messages to the destination side with minimal latency.
Users can expect to see bridge latencies well below 100 milliseconds provided the system is not overloaded as long and the product of the message size times the number of destination nodes is less than 2 megabytes.

Users can declare channels that contain bulk data by setting the "bulk_data" option on the channel definition.
When "bulk_data" is set to "true", the bridge will spread out message transmission over a configurable time interval to reduce the impact of bulk data transfers on other latency sensitive channels.
