# Pinion

Pinion is the underlying message-passing layer for Clockwork to enable communication between Cogs.

## Buffer

The underlying storage for all data communicated on a channel.
Each buffer contains multiple elements or "slots" that are contiguous in memory.
A buffer's API is low level and provides all control over the underlying data.
In general, nothing outside of the Clockwork infrastructure should be holding a reference to the buffer itself.
Some exceptions may include the logger or a spy tool.
All Cogs should be holding either a subscriber handle or a publisher handle that wraps the buffer API to expose only the necessary capabilities for that component.

## Slot

A slot is the interface that allows access to one element within a buffer.
Each slot has multiple fields: headers, message payload, and footers.
Components of the Clockwork infrastructure can use the slot to read / write data.

## Subscriber handle

An API that wraps the underlying buffer allowing a subscriber to read the available messages.
A subscriber handle does not store data specific to one subscriber.
It merely makes the raw API of the buffer more ergonomic for a subscriber.
Given there is no subscriber specific state, a Cog could have its own subscriber handle or it could share one with all other Cogs in the progress that subscribe to the same channel.
